#!/usr/bin/env python3
#
# LibProsperoPkg - A library for building and inspecting PS5 packages.
# C++ port/rewrite Copyright (C) 2026 seregonwar.

from __future__ import annotations

import argparse
import ctypes
import json
import os
import platform
import struct
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


EXPECTED_EXPORTS = [
    "lpp_version",
    "lpp_last_error",
    "lpp_is_valid_content_id",
    "lpp_is_valid_title_id",
    "lpp_compose_content_id",
    "lpp_build_package",
    "lpp_detect_package_type",
    "lpp_build_inner_image",
    "lpp_encrypt_pfs_image",
    "lpp_pack_pfs_image",
    "lpp_unpack_pfs_image",
    "lpp_is_self",
    "lpp_is_elf",
    "lpp_is_ucp",
    "lpp_make_fself",
]

UNPORTED_CPP_FEATURES: list[str] = []


@dataclass
class Check:
    name: str
    status: str
    details: str = ""


def find_default_cpp_lib() -> Path:
    suffixes = {
        "Darwin": [".dylib"],
        "Linux": [".so"],
        "Windows": [".dll"],
    }.get(platform.system(), [".so", ".dylib", ".dll"])
    candidates: list[Path] = []
    for suffix in suffixes:
        candidates.extend(Path("build").glob(f"LibProsperoPkg*{suffix}"))
        candidates.extend(Path("build-ninja-release").glob(f"LibProsperoPkg*{suffix}"))
    for path in candidates:
        if path.is_file() and "dSYM" not in path.name:
            return path
    raise FileNotFoundError("Could not find C++ LibProsperoPkg shared library; pass --cpp-lib")


def find_baseline_lib(directory: Path) -> Path:
    for name in ("LibProsperoPkg.dylib", "LibProsperoPkg.so", "LibProsperoPkg.dll"):
        path = directory / name
        if path.exists():
            return path
    matches = list(directory.glob("LibProsperoPkg*"))
    if matches:
        return matches[0]
    raise FileNotFoundError(f"Could not find baseline LibProsperoPkg shared library in {directory}")


def load_library(path: Path) -> ctypes.CDLL:
    return ctypes.CDLL(str(path.resolve()))


def configure_library(lib: ctypes.CDLL) -> None:
    lib.lpp_version.restype = ctypes.c_char_p

    lib.lpp_last_error.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.lpp_last_error.restype = ctypes.c_int

    lib.lpp_is_valid_content_id.argtypes = [ctypes.c_char_p]
    lib.lpp_is_valid_content_id.restype = ctypes.c_int

    lib.lpp_is_valid_title_id.argtypes = [ctypes.c_char_p]
    lib.lpp_is_valid_title_id.restype = ctypes.c_int

    lib.lpp_compose_content_id.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    lib.lpp_compose_content_id.restype = ctypes.c_int

    lib.lpp_build_package.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    lib.lpp_build_package.restype = ctypes.c_int

    lib.lpp_detect_package_type.argtypes = [ctypes.c_char_p]
    lib.lpp_detect_package_type.restype = ctypes.c_int

    lib.lpp_build_inner_image.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_int,
    ]
    lib.lpp_build_inner_image.restype = ctypes.c_int

    lib.lpp_encrypt_pfs_image.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
    lib.lpp_encrypt_pfs_image.restype = ctypes.c_int

    lib.lpp_is_self.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.lpp_is_self.restype = ctypes.c_int

    lib.lpp_is_elf.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.lpp_is_elf.restype = ctypes.c_int

    lib.lpp_is_ucp.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.lpp_is_ucp.restype = ctypes.c_int

    lib.lpp_make_fself.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    lib.lpp_make_fself.restype = ctypes.c_int

    lib.lpp_pack_pfs_image.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
    lib.lpp_pack_pfs_image.restype = ctypes.c_int

    lib.lpp_unpack_pfs_image.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    lib.lpp_unpack_pfs_image.restype = ctypes.c_longlong


def c_buffer(data: bytes) -> ctypes.Array[ctypes.c_char]:
    return ctypes.create_string_buffer(data, len(data))


def make_test_elf() -> bytes:
    data = bytearray(0x120)
    data[0:7] = b"\x7fELF\x02\x01\x01"

    def le16(offset: int, value: int) -> None:
        data[offset:offset + 2] = struct.pack("<H", value)

    def le32(offset: int, value: int) -> None:
        data[offset:offset + 4] = struct.pack("<I", value)

    def le64(offset: int, value: int) -> None:
        data[offset:offset + 8] = struct.pack("<Q", value)

    le16(0x10, 0x02)
    le16(0x12, 0x3E)
    le32(0x14, 1)
    le64(0x20, 0x40)
    le16(0x34, 0x40)
    le16(0x36, 0x38)
    le16(0x38, 1)

    ph = 0x40
    le32(ph + 0x00, 1)
    le32(ph + 0x04, 5)
    le64(ph + 0x08, 0x100)
    le64(ph + 0x10, 0x400000)
    le64(ph + 0x18, 0x400000)
    le64(ph + 0x20, 0x10)
    le64(ph + 0x28, 0x10)
    le64(ph + 0x30, 0x10)

    for index in range(0x10):
        data[0x100 + index] = 0xA0 + index
    return bytes(data)


def make_ucp_probe() -> bytes:
    data = bytearray(0x60)
    data[0:4] = struct.pack(">I", 0xB228C60A)
    data[4:8] = struct.pack(">I", 1)
    data[8:16] = struct.pack(">Q", len(data))
    data[0x10:0x14] = struct.pack(">I", 0)
    data[0x14:0x18] = struct.pack(">I", 0x40)
    return bytes(data)


def make_pfs_image_probe() -> bytes:
    data = bytearray(0x3000)
    struct.pack_into("<q", data, 0x00, 2)
    struct.pack_into("<q", data, 0x08, 20130315)
    struct.pack_into("<H", data, 0x1C, 0x8)
    struct.pack_into("<I", data, 0x20, 0x1000)
    data[0x370:0x380] = bytes(range(16))
    for index in range(0x1000, len(data)):
        data[index] = (index * 13 + 0x41) & 0xFF
    return bytes(data)


def compose(lib: ctypes.CDLL, publisher: bytes | None, title_id: bytes | None,
            label: bytes | None, capacity: int = 64) -> tuple[int, bytes]:
    buffer = ctypes.create_string_buffer(max(capacity, 1))
    rc = lib.lpp_compose_content_id(publisher, title_id, label, buffer, capacity)
    return rc, buffer.value


def make_fself(lib: ctypes.CDLL, elf: bytes) -> tuple[int, bytes]:
    elf_buffer = c_buffer(elf)
    required = lib.lpp_make_fself(elf_buffer, len(elf), None, 0)
    if required <= 0:
        return required, b""
    out = ctypes.create_string_buffer(required)
    rc = lib.lpp_make_fself(elf_buffer, len(elf), out, required)
    return rc, out.raw[:max(rc, 0)]


def last_error(lib: ctypes.CDLL) -> str:
    buffer = ctypes.create_string_buffer(2048)
    rc = lib.lpp_last_error(buffer, len(buffer))
    if rc <= 0:
        return ""
    return buffer.value.decode("utf-8", "replace")


def pfsc_pack(lib: ctypes.CDLL, input_path: Path, output_path: Path,
              compression_level: int = 6, block_size: int = 0x10000) -> int:
    return lib.lpp_pack_pfs_image(
        os.fsencode(input_path),
        os.fsencode(output_path),
        compression_level,
        block_size,
    )


def pfsc_unpack(lib: ctypes.CDLL, input_path: Path, output_path: Path) -> int:
    return lib.lpp_unpack_pfs_image(os.fsencode(input_path), os.fsencode(output_path))


def make_build_source_tree(root: Path) -> Path:
    source = root / "source"
    (source / "sce_sys").mkdir(parents=True)
    (source / "data" / "nested").mkdir(parents=True)
    (source / "sce_sys" / "param.json").write_text(
        '{"contentId":"UP9000-PPSA00000_00-PROSPERO00000000","titleId":"PPSA00000"}\n',
        encoding="utf-8",
    )
    (source / "eboot.bin").write_bytes(b"ELF placeholder\n")
    (source / "data" / "nested" / "asset.txt").write_text("native build fixture\n", encoding="utf-8")
    (source / "ignored.gp5").write_text("should not enter native image\n", encoding="utf-8")
    return source


def exports(path: Path) -> list[str]:
    system = platform.system()
    commands: list[list[str]]
    if system == "Darwin":
        commands = [["nm", "-gU", str(path)]]
    elif system == "Linux":
        commands = [["nm", "-D", "--defined-only", str(path)]]
    else:
        return []

    try:
        output = subprocess.check_output(commands[0], text=True, stderr=subprocess.DEVNULL)
    except (OSError, subprocess.CalledProcessError):
        return []

    found: list[str] = []
    for line in output.splitlines():
        symbol = line.split()[-1] if line.split() else ""
        if system == "Darwin" and symbol.startswith("_"):
            symbol = symbol[1:]
        if symbol.startswith("lpp_"):
            found.append(symbol)
    return sorted(set(found))


def file_size(path: Path) -> int:
    return path.stat().st_size if path.exists() else 0


def tree_size(path: Path) -> int:
    if path.is_file():
        return file_size(path)
    total = 0
    for child in path.rglob("*"):
        if child.is_file():
            total += child.stat().st_size
    return total


def tool_sizes(directory: Path) -> dict[str, int]:
    suffix = ".exe" if platform.system() == "Windows" else ""
    tools = sorted(
        path
        for path in directory.glob(f"prosperopkg-*{suffix}")
        if path.is_file() and os.access(path, os.X_OK)
    )
    return {path.name: file_size(path) for path in tools}


def display_path(path: str) -> str:
    value = Path(path)
    try:
        return str(value.relative_to(Path.cwd()))
    except ValueError:
        return str(value)


def check_equal(checks: list[Check], name: str, lhs: Any, rhs: Any, details: str = "") -> None:
    if lhs == rhs:
        checks.append(Check(name, "PASS", details or str(lhs)))
    else:
        checks.append(Check(name, "FAIL", f"csharp={lhs!r} cpp={rhs!r} {details}".strip()))


def benchmark(name: str, iterations: int, csharp_call: Callable[[], Any], cpp_call: Callable[[], Any]) -> dict[str, Any]:
    csharp_call()
    cpp_call()

    start = time.perf_counter_ns()
    for _ in range(iterations):
        csharp_call()
    csharp_ns = time.perf_counter_ns() - start

    start = time.perf_counter_ns()
    for _ in range(iterations):
        cpp_call()
    cpp_ns = time.perf_counter_ns() - start

    csharp_us = csharp_ns / iterations / 1000.0
    cpp_us = cpp_ns / iterations / 1000.0
    speedup = csharp_us / cpp_us if cpp_us else None
    return {
        "name": name,
        "iterations": iterations,
        "csharp_us": csharp_us,
        "cpp_us": cpp_us,
        "speedup": speedup,
    }


def run(args: argparse.Namespace) -> dict[str, Any]:
    baseline_dir = args.baseline_dir.resolve()
    baseline_lib_path = find_baseline_lib(baseline_dir)
    cpp_lib_path = args.cpp_lib.resolve() if args.cpp_lib else find_default_cpp_lib().resolve()
    cpp_tools_dir = args.cpp_tools_dir.resolve() if args.cpp_tools_dir else cpp_lib_path.parent

    csharp = load_library(baseline_lib_path)
    cpp = load_library(cpp_lib_path)
    configure_library(csharp)
    configure_library(cpp)

    checks: list[Check] = []
    baseline_exports = exports(baseline_lib_path)
    cpp_exports = exports(cpp_lib_path)
    if baseline_exports and cpp_exports:
        check_equal(checks, "exported-lpp-functions", baseline_exports, cpp_exports)
        check_equal(checks, "expected-export-count", len(cpp_exports), len(EXPECTED_EXPORTS))

    csharp_version = csharp.lpp_version().decode("utf-8", "replace")
    cpp_version = cpp.lpp_version().decode("utf-8", "replace")
    checks.append(Check("version-string", "INFO", f"csharp={csharp_version}; cpp={cpp_version}"))

    content_ids = [
        b"UP9000-PPSA00000_00-PROSPERO00000000",
        b"up9000-PPSA00000_00-PROSPERO00000000",
        b"bad",
        None,
    ]
    for value in content_ids:
        check_equal(
            checks,
            f"is-valid-content-id:{value!r}",
            csharp.lpp_is_valid_content_id(value),
            cpp.lpp_is_valid_content_id(value),
        )

    title_ids = [b"PPSA00000", b"CUSA00000", b"abcd12345", b"PPSA0000", None]
    for value in title_ids:
        check_equal(
            checks,
            f"is-valid-title-id:{value!r}",
            csharp.lpp_is_valid_title_id(value),
            cpp.lpp_is_valid_title_id(value),
        )

    compose_cases = [
        (None, None, None, 64),
        (b"up9", b"ppsa1", b"hello world!", 64),
        (b"EU1234", b"ABCD12345", b"a&b", 64),
        (None, None, None, 4),
    ]
    for case in compose_cases:
        check_equal(checks, f"compose:{case!r}", compose(csharp, *case), compose(cpp, *case))

    elf = make_test_elf()
    elf_buf = c_buffer(elf)
    check_equal(checks, "is-elf:test-elf", csharp.lpp_is_elf(elf_buf, len(elf)), cpp.lpp_is_elf(elf_buf, len(elf)))
    check_equal(checks, "is-self:test-elf", csharp.lpp_is_self(elf_buf, len(elf)), cpp.lpp_is_self(elf_buf, len(elf)))

    ucp = make_ucp_probe()
    ucp_buf = c_buffer(ucp)
    check_equal(checks, "is-ucp:minimal-header", csharp.lpp_is_ucp(ucp_buf, len(ucp)), cpp.lpp_is_ucp(ucp_buf, len(ucp)))

    csharp_fself_rc, csharp_fself = make_fself(csharp, elf)
    cpp_fself_rc, cpp_fself = make_fself(cpp, elf)
    check_equal(checks, "make-fself:size", csharp_fself_rc, cpp_fself_rc)
    check_equal(checks, "make-fself:bytes", csharp_fself, cpp_fself, "exact byte comparison")
    if csharp_fself and cpp_fself:
        csharp_self_buf = c_buffer(csharp_fself)
        cpp_self_buf = c_buffer(cpp_fself)
        check_equal(
            checks,
            "make-fself:is-self",
            csharp.lpp_is_self(csharp_self_buf, len(csharp_fself)),
            cpp.lpp_is_self(cpp_self_buf, len(cpp_fself)),
        )

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        fixtures = {
            "meta": (b"\x7fCNTxx", 0),
            "full-debug": (b"\x7fFIH\x00\x00", 2),
            "full-retail": (b"\x7fFIH\x00\x80", 1),
            "unknown": (b"notpkg", -1),
        }
        for name, (payload, expected) in fixtures.items():
            path = tmpdir / f"{name}.pkg"
            path.write_bytes(payload)
            path_bytes = os.fsencode(path)
            csharp_rc = csharp.lpp_detect_package_type(path_bytes)
            cpp_rc = cpp.lpp_detect_package_type(path_bytes)
            check_equal(checks, f"detect-package-type:{name}", csharp_rc, cpp_rc, f"expected={expected}")

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        content_id = b"UP9000-PPSA00000_00-PROSPERO00000000"
        passcode = b"00000000000000000000000000000000"
        pfs_probe = make_pfs_image_probe()
        csharp_path = tmpdir / "csharp-pfs.img"
        cpp_path = tmpdir / "cpp-pfs.img"
        csharp_path.write_bytes(pfs_probe)
        cpp_path.write_bytes(pfs_probe)

        csharp_rc = csharp.lpp_encrypt_pfs_image(os.fsencode(csharp_path), content_id, passcode)
        cpp_rc = cpp.lpp_encrypt_pfs_image(os.fsencode(cpp_path), content_id, passcode)
        if csharp_rc == 0 and cpp_rc == 0:
            csharp_bytes = csharp_path.read_bytes()
            cpp_bytes = cpp_path.read_bytes()
            if csharp_bytes == cpp_bytes:
                checks.append(Check(
                    "encrypt-pfs-image:bytes",
                    "PASS",
                    f"exact byte comparison; bytes={len(cpp_bytes)}",
                ))
            else:
                first_diff = next(
                    (index for index, (lhs, rhs) in enumerate(zip(csharp_bytes, cpp_bytes)) if lhs != rhs),
                    min(len(csharp_bytes), len(cpp_bytes)),
                )
                checks.append(Check(
                    "encrypt-pfs-image:bytes",
                    "FAIL",
                    f"first_diff={first_diff}; csharp_bytes={len(csharp_bytes)}; cpp_bytes={len(cpp_bytes)}",
                ))
        else:
            checks.append(Check(
                "encrypt-pfs-image:bytes",
                "FAIL",
                f"csharp_rc={csharp_rc} csharp_error={last_error(csharp) or 'not reported'}; "
                f"cpp_rc={cpp_rc} cpp_error={last_error(cpp) or 'not reported'}",
            ))

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        raw_path = tmpdir / "pfsc-raw.img"
        csharp_pfsc_path = tmpdir / "csharp.pfsc"
        csharp_unpacked_path = tmpdir / "csharp-unpacked.img"
        cpp_pfsc_path = tmpdir / "cpp.pfsc"
        cpp_unpacked_path = tmpdir / "cpp-unpacked.img"
        raw = bytes((index * 31 + 7) & 0xFF for index in range(0x20000))
        raw_path.write_bytes(raw)

        csharp_pack_rc = pfsc_pack(csharp, raw_path, csharp_pfsc_path)
        if csharp_pack_rc == 0 and csharp_pfsc_path.exists():
            csharp_unpack_rc = pfsc_unpack(csharp, csharp_pfsc_path, csharp_unpacked_path)
            csharp_ok = (
                csharp_unpack_rc == len(raw) and
                csharp_unpacked_path.exists() and
                csharp_unpacked_path.read_bytes() == raw
            )
            checks.append(Check(
                "pfsc-pack-unpack:csharp-baseline",
                "PASS" if csharp_ok else "FAIL",
                f"pack_rc={csharp_pack_rc}; unpack_rc={csharp_unpack_rc}; bytes={len(raw)}",
            ))
        else:
            checks.append(Check(
                "pfsc-pack-unpack:csharp-baseline",
                "INFO",
                f"pack_rc={csharp_pack_rc}; error={last_error(csharp) or 'not reported'}",
            ))

        cpp_pack_rc = pfsc_pack(cpp, raw_path, cpp_pfsc_path)
        if cpp_pack_rc != 0:
            checks.append(Check(
                "pfsc-pack-unpack:cpp-zlib",
                "FAIL",
                f"pack_rc={cpp_pack_rc}; error={last_error(cpp) or 'not reported'}",
            ))
        else:
            cpp_unpack_rc = pfsc_unpack(cpp, cpp_pfsc_path, cpp_unpacked_path)
            cpp_ok = (
                cpp_unpack_rc == len(raw) and
                cpp_unpacked_path.exists() and
                cpp_unpacked_path.read_bytes() == raw
            )
            checks.append(Check(
                "pfsc-pack-unpack:cpp-zlib",
                "PASS" if cpp_ok else "FAIL",
                f"pack_rc={cpp_pack_rc}; unpack_rc={cpp_unpack_rc}; bytes={len(raw)}",
            ))

        cpp_pfsv3_path = tmpdir / "cpp-pfsv3.pfsc"
        cpp_pfsv3_unpacked_path = tmpdir / "cpp-pfsv3-unpacked.img"
        uneven_raw = bytes((index * 17 + 0x22) & 0xFF for index in range(0x41021))
        uneven_path = tmpdir / "pfsv3-source.img"
        uneven_path.write_bytes(uneven_raw)
        cpp_pfsv3_rc = pfsc_pack(cpp, uneven_path, cpp_pfsv3_path, -7, 0x40000)
        if cpp_pfsv3_rc != 0:
            checks.append(Check(
                "pfsc-pfsv3-stored:cpp-cabi",
                "FAIL",
                f"pack_rc={cpp_pfsv3_rc}; error={last_error(cpp) or 'not reported'}",
            ))
        else:
            data = cpp_pfsv3_path.read_bytes()
            version = struct.unpack_from("<H", data, 0x04)[0] if len(data) >= 0x06 else 0
            cpp_pfsv3_unpack_rc = pfsc_unpack(cpp, cpp_pfsv3_path, cpp_pfsv3_unpacked_path)
            cpp_pfsv3_ok = (
                version == 3 and
                cpp_pfsv3_unpack_rc == len(uneven_raw) and
                cpp_pfsv3_unpacked_path.exists() and
                cpp_pfsv3_unpacked_path.read_bytes() == uneven_raw
            )
            checks.append(Check(
                "pfsc-pfsv3-stored:cpp-cabi",
                "PASS" if cpp_pfsv3_ok else "FAIL",
                f"pack_rc={cpp_pfsv3_rc}; unpack_rc={cpp_pfsv3_unpack_rc}; version={version}; bytes={len(data)}",
            ))

    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = Path(tmp)
        source = make_build_source_tree(tmpdir)
        content_id = b"UP9000-PPSA00000_00-PROSPERO00000000"
        passcode = b"00000000000000000000000000000000"

        inner_path = tmpdir / "inner.img"
        out = ctypes.create_string_buffer(4096)
        inner_rc = cpp.lpp_build_inner_image(
            os.fsencode(source),
            os.fsencode(inner_path),
            content_id,
            passcode,
            1,
            out,
            len(out),
        )
        if inner_rc == 0 and inner_path.exists():
            data = inner_path.read_bytes()
            mode = struct.unpack_from("<H", data, 0x1C)[0] if len(data) >= 0x1E else 0
            checks.append(Check(
                "build-inner-image:cpp-cabi",
                "PASS" if len(data) >= 0x20000 and (mode & 0x4) else "FAIL",
                f"path={display_path(out.value.decode('utf-8', 'replace'))}; bytes={len(data)}; mode=0x{mode:x}",
            ))
        else:
            checks.append(Check(
                "build-inner-image:cpp-cabi",
                "FAIL",
                f"rc={inner_rc}; error={last_error(cpp) or 'not reported'}",
            ))

        kraken_inner_path = tmpdir / "inner-pfsv3.img"
        kraken_out = ctypes.create_string_buffer(4096)
        kraken_inner_rc = cpp.lpp_build_inner_image(
            os.fsencode(source),
            os.fsencode(kraken_inner_path),
            content_id,
            passcode,
            3,
            kraken_out,
            len(kraken_out),
        )
        if kraken_inner_rc == 0 and kraken_inner_path.exists():
            data = kraken_inner_path.read_bytes()
            version = struct.unpack_from("<H", data, 0x04)[0] if len(data) >= 0x06 else 0
            checks.append(Check(
                "build-inner-image-pfsv3:cpp-cabi",
                "PASS" if version == 3 and len(data) > 0x400 else "FAIL",
                f"path={display_path(kraken_out.value.decode('utf-8', 'replace'))}; bytes={len(data)}; version={version}",
            ))
        else:
            checks.append(Check(
                "build-inner-image-pfsv3:cpp-cabi",
                "FAIL",
                f"rc={kraken_inner_rc}; error={last_error(cpp) or 'not reported'}",
            ))

        package_dir = tmpdir / "pkg"
        package_dir.mkdir()
        pkg_out = ctypes.create_string_buffer(4096)
        package_rc = cpp.lpp_build_package(
            os.fsencode(source),
            os.fsencode(package_dir),
            content_id,
            passcode,
            b"Native Fixture",
            b"PPSA00000",
            b"01.23",
            0,
            1,
            1,
            pkg_out,
            len(pkg_out),
        )
        package_path = Path(pkg_out.value.decode("utf-8", "replace")) if pkg_out.value else Path()
        if package_rc == 0 and package_path.exists():
            detected = cpp.lpp_detect_package_type(os.fsencode(package_path))
            checks.append(Check(
                "build-package:cpp-cabi",
                "PASS" if detected == 2 else "FAIL",
                f"path={display_path(str(package_path))}; bytes={package_path.stat().st_size}; detected={detected}",
            ))
        else:
            checks.append(Check(
                "build-package:cpp-cabi",
                "FAIL",
                f"rc={package_rc}; error={last_error(cpp) or 'not reported'}",
            ))

    perf: list[dict[str, Any]] = []
    iterations = max(args.iterations, 1)
    perf.append(benchmark(
        "is_valid_content_id",
        iterations * 200,
        lambda: csharp.lpp_is_valid_content_id(b"UP9000-PPSA00000_00-PROSPERO00000000"),
        lambda: cpp.lpp_is_valid_content_id(b"UP9000-PPSA00000_00-PROSPERO00000000"),
    ))
    perf.append(benchmark(
        "compose_content_id",
        iterations * 50,
        lambda: compose(csharp, b"up9", b"ppsa1", b"hello world!", 64),
        lambda: compose(cpp, b"up9", b"ppsa1", b"hello world!", 64),
    ))
    perf.append(benchmark(
        "is_elf",
        iterations * 200,
        lambda: csharp.lpp_is_elf(elf_buf, len(elf)),
        lambda: cpp.lpp_is_elf(elf_buf, len(elf)),
    ))
    perf.append(benchmark(
        "make_fself",
        iterations,
        lambda: make_fself(csharp, elf),
        lambda: make_fself(cpp, elf),
    ))

    sizes = {
        "csharp_lib_bytes": file_size(baseline_lib_path),
        "csharp_bundle_bytes": tree_size(baseline_dir),
        "cpp_lib_bytes": file_size(cpp_lib_path),
        "cpp_tools_bytes": tool_sizes(cpp_tools_dir),
    }
    sizes["cpp_tools_total_bytes"] = sum(sizes["cpp_tools_bytes"].values())
    sizes["cpp_bundle_bytes"] = sizes["cpp_lib_bytes"] + sizes["cpp_tools_total_bytes"]

    return {
        "baseline_dir": str(baseline_dir),
        "baseline_lib": str(baseline_lib_path),
        "cpp_lib": str(cpp_lib_path),
        "cpp_tools_dir": str(cpp_tools_dir),
        "checks": [check.__dict__ for check in checks],
        "performance": perf,
        "sizes": sizes,
        "unported_cpp_features": UNPORTED_CPP_FEATURES,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# LibProsperoPkg Native Comparison",
        "",
        f"- C# baseline: `{display_path(report['baseline_lib'])}`",
        f"- C++ library: `{display_path(report['cpp_lib'])}`",
        "",
        "## Correctness",
        "",
        "| Check | Status | Details |",
        "|---|---:|---|",
    ]
    for check in report["checks"]:
        lines.append(f"| `{check['name']}` | {check['status']} | {check['details']} |")

    lines.extend([
        "",
        "## Performance",
        "",
        "| Case | Iterations | C# us/call | C++ us/call | Speedup |",
        "|---|---:|---:|---:|---:|",
    ])
    for item in report["performance"]:
        speedup = item["speedup"]
        speedup_text = "" if speedup is None else f"{speedup:.2f}x"
        lines.append(
            f"| `{item['name']}` | {item['iterations']} | "
            f"{item['csharp_us']:.3f} | {item['cpp_us']:.3f} | {speedup_text} |"
        )

    sizes = report["sizes"]
    lines.extend([
        "",
        "## Binary Size",
        "",
        "| Artifact | Bytes |",
        "|---|---:|",
        f"| C# `LibProsperoPkg` | {sizes['csharp_lib_bytes']} |",
        f"| C# baseline folder | {sizes['csharp_bundle_bytes']} |",
        f"| C++ `LibProsperoPkg` | {sizes['cpp_lib_bytes']} |",
        f"| C++ tools total | {sizes['cpp_tools_total_bytes']} |",
        f"| C++ library + tools | {sizes['cpp_bundle_bytes']} |",
    ])
    for name, size in sizes["cpp_tools_bytes"].items():
        lines.append(f"| C++ `{name}` | {size} |")

    lines.extend([
        "",
        "## Known C++ Gaps",
        "",
    ])
    if report["unported_cpp_features"]:
        for name in report["unported_cpp_features"]:
            lines.append(f"- `{name}`")
    else:
        lines.append("- None in the exported C ABI surface.")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def print_summary(report: dict[str, Any]) -> None:
    print("Correctness:")
    for check in report["checks"]:
        print(f"  {check['status']:>4} {check['name']} {check['details']}")

    print("\nPerformance:")
    for item in report["performance"]:
        speedup = item["speedup"]
        speedup_text = "" if speedup is None else f"{speedup:.2f}x"
        print(
            f"  {item['name']}: C# {item['csharp_us']:.3f} us/call, "
            f"C++ {item['cpp_us']:.3f} us/call, speedup {speedup_text}"
        )

    print("\nBinary size:")
    sizes = report["sizes"]
    print(f"  C# lib:            {sizes['csharp_lib_bytes']} bytes")
    print(f"  C# baseline dir:   {sizes['csharp_bundle_bytes']} bytes")
    print(f"  C++ lib:           {sizes['cpp_lib_bytes']} bytes")
    print(f"  C++ tools total:   {sizes['cpp_tools_total_bytes']} bytes")
    print(f"  C++ lib + tools:   {sizes['cpp_bundle_bytes']} bytes")


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare C# NativeAOT and C++ LibProsperoPkg native APIs.")
    parser.add_argument("--baseline-dir", type=Path, default=Path("test/libprosperopkg-osx-arm64"))
    parser.add_argument("--cpp-lib", type=Path)
    parser.add_argument("--cpp-tools-dir", type=Path)
    parser.add_argument("--iterations", type=int, default=500)
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    args = parser.parse_args()

    report = run(args)
    print_summary(report)

    if args.json_out:
        args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.markdown_out:
        write_markdown(report, args.markdown_out)

    failed = [check for check in report["checks"] if check["status"] == "FAIL"]
    if failed:
        print(f"\n{len(failed)} correctness check(s) failed.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
