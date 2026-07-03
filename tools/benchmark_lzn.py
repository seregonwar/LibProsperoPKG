#!/usr/bin/env python3
#
# LibProsperoPkg - A library for building and inspecting PS5 packages.
# Copyright (C) 2026 seregonwar.

from __future__ import annotations

import argparse
import platform
import re
import subprocess
import tempfile
import time
import zlib
from dataclasses import dataclass
from pathlib import Path


BENCH_RE = {
    "compressed": re.compile(r"compressed:\s+(\d+)\s+bytes"),
    "ratio": re.compile(r"ratio:\s+([0-9.]+)"),
    "compress": re.compile(r"compress:\s+([0-9.]+)\s+MiB/s"),
    "decompress": re.compile(r"decompress:\s+([0-9.]+)\s+MiB/s"),
}


@dataclass(frozen=True)
class Fixture:
    name: str
    path: Path
    size: int


@dataclass(frozen=True)
class BenchResult:
    fixture: str
    codec: str
    input_bytes: int
    compressed_bytes: int
    ratio: float
    compress_mib_s: float
    decompress_mib_s: float


def default_tool() -> Path:
    suffix = ".exe" if platform.system() == "Windows" else ""
    return Path("build-ninja-release") / f"prosperopkg-lzn{suffix}"


def write_fixtures(root: Path, size_mib: int) -> list[Fixture]:
    size = size_mib * 1024 * 1024

    repeated_phrase = b"LibProsperoPkg LZN clean-room benchmark block. "
    repeated = bytearray()
    while len(repeated) < size:
        repeated.extend(repeated_phrase)

    structured = bytearray(size)
    for index in range(size):
        structured[index] = ((index // 64) + (index // 65536) * 17) & 0xFF

    sparse = bytearray(size)
    for offset in range(0, size, 4096):
        sparse[offset:offset + 16] = b"LZN-SPARSE-BLOCK!"
        sparse[offset + 512:offset + 768] = bytes([(offset // 4096) & 0xFF]) * 256

    entropy = bytearray(size)
    state = 0x1234ABCD
    for index in range(size):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        entropy[index] = state & 0xFF

    fixtures = {
        "repeated-text": bytes(repeated[:size]),
        "structured-pfs-like": bytes(structured),
        "sparse-pages": bytes(sparse),
        "high-entropy": bytes(entropy),
    }

    out: list[Fixture] = []
    for name, data in fixtures.items():
        path = root / f"{name}.bin"
        path.write_bytes(data)
        out.append(Fixture(name, path, len(data)))
    return out


def parse_lzn_bench(text: str, fixture: Fixture, codec: str) -> BenchResult:
    values: dict[str, float] = {}
    for key, regex in BENCH_RE.items():
        match = regex.search(text)
        if match is None:
            raise ValueError(f"Could not parse {key!r} from prosperopkg-lzn output:\n{text}")
        values[key] = float(match.group(1))

    return BenchResult(
        fixture=fixture.name,
        codec=codec,
        input_bytes=fixture.size,
        compressed_bytes=int(values["compressed"]),
        ratio=values["ratio"],
        compress_mib_s=values["compress"],
        decompress_mib_s=values["decompress"],
    )


def verify_lzn_roundtrip(tool: Path, fixture: Fixture, root: Path, level: int) -> None:
    compressed = root / f"{fixture.name}.lzn"
    restored = root / f"{fixture.name}.restored"
    subprocess.run(
        [str(tool), "compress", str(fixture.path), str(compressed), str(level)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        [str(tool), "decompress", str(compressed), str(restored)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    if restored.read_bytes() != fixture.path.read_bytes():
        raise RuntimeError(f"LZN round-trip mismatch for {fixture.name}")


def bench_lzn(tool: Path, fixture: Fixture, iterations: int, level: int) -> BenchResult:
    completed = subprocess.run(
        [str(tool), "bench", str(fixture.path), str(iterations), str(level)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return parse_lzn_bench(completed.stdout, fixture, "LZN1-frame")


def verify_lznb_roundtrip(tool: Path, fixture: Fixture, root: Path, level: int, block_size: int) -> None:
    compressed = root / f"{fixture.name}.lznb"
    restored = root / f"{fixture.name}.lznb.restored"
    subprocess.run(
        [str(tool), "block-compress", str(fixture.path), str(compressed), str(level), str(block_size)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        [str(tool), "block-decompress", str(compressed), str(restored)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    if restored.read_bytes() != fixture.path.read_bytes():
        raise RuntimeError(f"LZNB round-trip mismatch for {fixture.name}")


def bench_lznb(tool: Path, fixture: Fixture, iterations: int, level: int, block_size: int) -> BenchResult:
    completed = subprocess.run(
        [str(tool), "block-bench", str(fixture.path), str(iterations), str(level), str(block_size)],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return parse_lzn_bench(completed.stdout, fixture, "LZNB-block")


def bench_zlib(fixture: Fixture, iterations: int, level: int) -> BenchResult:
    data = fixture.path.read_bytes()
    compressed = zlib.compress(data, level)
    sink = 0

    start = time.perf_counter_ns()
    for _ in range(iterations):
        compressed = zlib.compress(data, level)
        sink += len(compressed)
    compress_ns = time.perf_counter_ns() - start

    start = time.perf_counter_ns()
    for _ in range(iterations):
        restored = zlib.decompress(compressed)
        sink += len(restored)
    decompress_ns = time.perf_counter_ns() - start

    if restored != data or sink == 0:
        raise RuntimeError(f"zlib round-trip mismatch for {fixture.name}")

    mib = len(data) / (1024.0 * 1024.0)
    return BenchResult(
        fixture=fixture.name,
        codec=f"zlib-{level}",
        input_bytes=len(data),
        compressed_bytes=len(compressed),
        ratio=len(compressed) / len(data) if data else 1.0,
        compress_mib_s=mib * iterations / (compress_ns / 1_000_000_000.0),
        decompress_mib_s=mib * iterations / (decompress_ns / 1_000_000_000.0),
    )


def write_markdown(path: Path, results: list[BenchResult], args: argparse.Namespace) -> None:
    lines = [
        "# LZN Benchmark",
        "",
        f"- Tool: `{args.tool}`",
        f"- Fixture size: {args.size_mib} MiB each",
        f"- Iterations: {args.iterations}",
        f"- LZN level: {args.level}",
        f"- LZNB block size: {args.block_size}",
        f"- zlib level: {args.zlib_level}",
        "",
        "Kraken/newLZ is not measured here because no licensed, clean-room-compatible oracle is configured in this repository yet.",
        "",
        "| Fixture | Codec | Input bytes | Compressed bytes | Ratio | Compress MiB/s | Decompress MiB/s |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for item in results:
        lines.append(
            f"| {item.fixture} | {item.codec} | {item.input_bytes} | {item.compressed_bytes} | "
            f"{item.ratio:.3f} | {item.compress_mib_s:.1f} | {item.decompress_mib_s:.1f} |"
        )
    lines.extend([
        "",
        "These numbers are a local optimization baseline.",
        "",
    ])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic LZN benchmark fixtures and report.")
    parser.add_argument("--tool", type=Path, default=default_tool())
    parser.add_argument("--out", type=Path, default=Path("reports/lzn-benchmark.md"))
    parser.add_argument("--size-mib", type=int, default=16)
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--level", type=int, default=2)
    parser.add_argument("--block-size", type=int, default=512 * 1024)
    parser.add_argument("--zlib-level", type=int, default=6)
    args = parser.parse_args()

    if not args.tool.exists():
        raise FileNotFoundError(f"prosperopkg-lzn not found: {args.tool}")
    if args.size_mib <= 0:
        raise ValueError("--size-mib must be positive")
    if args.iterations <= 0:
        raise ValueError("--iterations must be positive")

    with tempfile.TemporaryDirectory(prefix="lzn-bench-") as temp:
        root = Path(temp)
        fixtures = write_fixtures(root, args.size_mib)
        results: list[BenchResult] = []
        for fixture in fixtures:
            verify_lznb_roundtrip(args.tool, fixture, root, args.level, args.block_size)
            results.append(bench_lznb(args.tool, fixture, args.iterations, args.level, args.block_size))
            verify_lzn_roundtrip(args.tool, fixture, root, args.level)
            results.append(bench_lzn(args.tool, fixture, args.iterations, args.level))
            results.append(bench_zlib(fixture, args.iterations, args.zlib_level))

    write_markdown(args.out, results, args)
    print(f"Wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
