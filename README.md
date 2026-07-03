# LibProsperoPkg

LibProsperoPkg is now a cross-platform C++ rewrite of the original C#
LibProsperoPkg project. The native library and tools are authored by
**seregonwar**; the original C# project was created by **SvenGDK** and is kept
in `legacy/csharp` as a reference implementation while the port reaches full
feature parity.

The current tree is C++ first: the root CMake project builds the native static
library, the legacy-compatible C ABI shared library, command-line tools, and
native tests on Windows, Linux, and macOS.

## Current Scope

- `prosperopkg_cpp` static C++20 library.
- `LibProsperoPkg` C ABI shared library compatible with the legacy NativeAOT
  exported function set.
- CNT/FIH package parsing and CNT writing.
- Content-id and title-id helpers.
- CRC-32C, SHA-256, SHA3-256, HMAC-SHA256, AES-128, and AES-XTS helpers.
- PFS key derivation, PFS image transforms, and outer-PFS signature helpers.
- Legacy C ABI inner-PFS encryption with byte-for-byte parity against the C#
  NativeAOT baseline on deterministic seeded images.
- PFSC raw and zlib pack/unpack support, plus PFSv3 stored containers for the
  Kraken-compatible inner-image path.
- Experimental clean-room LZN1 frame codec with compression/decompression API
  and benchmark CLI. This is not copied from `ooz` and is not yet claimed as
  Kraken-compatible.
- LZNB block archive codec evolved from seregonwar's earlier block-codec design,
  with indexed blocks, CRC-32C validation, raw fallback, range decompression,
  and no mandatory LZ4/ZSTD dependency.
- Native source-folder inner image and CNT/FIH package builders exposed through
  the legacy C ABI.
- PS5 image digest helpers for package/body/rollup/entry/fixed-info/sblock data.
- UCP archive build/read/verify/repair support.
- SELF parsing and ELF-to-fake-SELF generation.
- GP5 project model, XML serialization, and folder-to-GP5 generation.
- CLI tools for inspection, fake-self generation, GP5 generation, key
  derivation, and LZN1 codec benchmarking.
- Native regression tests for the implemented library and tool behavior.
- C# NativeAOT vs C++ comparison script for API coverage, correctness,
  performance, and binary-size reporting.

`src/include/prosperopkg/psxpkg.h` is kept only as an optional GPL-3.0-or-later
reference reader by seregonwar. It is not linked into the CMake library or tools
unless a concrete need appears, and it is not installed into release packages.

## Requirements

| Component | Requirement |
|---|---|
| Build system | CMake 3.24 or newer |
| Language | C++20 |
| Compilers | MSVC, Clang, or GCC |
| Platforms | Windows, Linux, macOS |
| Optional | zlib, used when available for legacy PFSC compressed blocks |

No GUI is shipped today, but a native ImGui interface is in planning. The goal
is to keep the same practical workflow as the original tooling where a native
frontend makes sense, without adding a heavy runtime dependency to the core
library.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The build produces the static C++ library, the `LibProsperoPkg` shared C ABI,
and these tools when `LIBPROSPEROPKG_BUILD_TOOLS` is enabled:

| Tool | Purpose |
|---|---|
| `prosperopkg-inspect` | Inspect PKG/CNT/FIH, UCP, and SELF inputs. |
| `prosperopkg-fself` | Convert a 64-bit ELF into a fake SELF. |
| `prosperopkg-gp5` | Generate a GP5 project from a folder. |
| `prosperopkg-keys` | Derive EKPFS and PFS image keys. |
| `prosperopkg-lzn` | Compress, decompress, inspect, and benchmark LZN1/LZNB data. |

Useful CMake options:

| Option | Default | Purpose |
|---|---:|---|
| `LIBPROSPEROPKG_BUILD_C_API` | `ON` | Build the legacy-compatible shared C ABI. |
| `LIBPROSPEROPKG_BUILD_TOOLS` | `ON` | Build CLI tools. |
| `LIBPROSPEROPKG_BUILD_TESTS` | `ON` | Build and register tests. |
| `LIBPROSPEROPKG_OPTIMIZE_SIZE` | `ON` | Use size-oriented release flags and dead-code stripping. |
| `LIBPROSPEROPKG_ENABLE_IPO` | `ON` | Enable IPO/LTO when the compiler supports it. |
| `LIBPROSPEROPKG_VERSION` | `0.1.0` | Version embedded in the C ABI and CI release packages. |

## Test

```bash
ctest --test-dir build --build-config Release --output-on-failure
```

For local debug builds:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The native suite currently covers parser/writer round-trips, CLI behavior,
cryptographic helpers, GP5 generation, SELF/fake-SELF handling, UCP handling,
PFS transforms/signatures, and digest reporting. Full C# parity is still tracked
as migration work; the C# code in `legacy/csharp` remains the comparison oracle
for behavior that has not yet been ported.

To compare against a C# NativeAOT baseline, point CMake at a directory containing
`LibProsperoPkg.dylib`/`.so`/`.dll` and run CTest:

```bash
cmake -S . -B build-ninja-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLIBPROSPEROPKG_COMPARE_BASELINE=test/libprosperopkg-osx-arm64
cmake --build build-ninja-release --parallel
ctest --test-dir build-ninja-release --output-on-failure
```

For a standalone report:

```bash
python3 tools/compare_native_parity.py \
  --baseline-dir test/libprosperopkg-osx-arm64 \
  --cpp-lib build-ninja-release/LibProsperoPkg.dylib \
  --cpp-tools-dir build-ninja-release \
  --markdown-out reports/native-comparison.md
```

The latest local comparison is in [reports/native-comparison.md](reports/native-comparison.md).

Current release-build results on macOS arm64 against the local C# NativeAOT
baseline in `test/libprosperopkg-osx-arm64`:

| Check | C# NativeAOT | C++ native | Result |
|---|---:|---:|---:|
| Exported C ABI functions | 15 | 15 | match |
| `is_valid_content_id` | 1.028 us/call | 0.476 us/call | 2.16x faster |
| `compose_content_id` | 3.643 us/call | 2.234 us/call | 1.63x faster |
| `is_elf` | 0.906 us/call | 0.953 us/call | 0.95x |
| `make_fself` | 7.006 us/call | 4.395 us/call | 1.59x faster |
| Shared library size | 6,064,504 bytes | 164,392 bytes | 36.89x smaller |
| Baseline bundle vs C++ lib+tools | 34,378,430 bytes | 459,496 bytes | 74.82x smaller |

Correctness currently matches for the checked C ABI surfaces, including
content/title identifiers, fake-SELF generation, package type detection,
inner-PFS encryption, C++ PFSC zlib pack/unpack, PFSv3 stored pack/unpack,
native inner-image building, and native CNT/FIH package building. The local C#
baseline cannot pack PFSC on this host because its PFSv3 compression path
reports missing SHA3-256 support; the C++ PFSC paths still round-trip
successfully.

Clean-room LZN release-build benchmark on this macOS arm64 host
(`build-ninja-release`, `LIBPROSPEROPKG_OPTIMIZE_SIZE=ON`, 8 MiB synthetic
fixtures, 10 iterations, level 2, LZNB block size 512 KiB):

| Fixture | Codec | Ratio | Compress | Decompress |
|---|---|---:|---:|---:|
| Repeated text blocks | LZNB block | 0.023 | 124.5 MiB/s | 190.8 MiB/s |
| Repeated text blocks | LZN1 frame | 0.023 | 381.8 MiB/s | 1908.2 MiB/s |
| Repeated text blocks | zlib-6 | 0.003 | 343.1 MiB/s | 2465.7 MiB/s |
| Structured PFS-like bytes | LZNB block | 0.078 | 145.1 MiB/s | 197.3 MiB/s |
| Structured PFS-like bytes | LZN1 frame | 0.078 | 307.6 MiB/s | 731.4 MiB/s |
| Structured PFS-like bytes | zlib-6 | 0.007 | 239.8 MiB/s | 4320.3 MiB/s |

Release binary sizes from the same build:

| Binary | Size |
|---|---:|
| `prosperopkg-lzn` | 63,616 bytes |
| `prosperopkg-inspect` | 86,688 bytes |
| `LibProsperoPkg.0.1.0.dylib` | 164,392 bytes |

The full local table is in [reports/lzn-benchmark.md](reports/lzn-benchmark.md).
These numbers do not prove Kraken superiority. In the current fixtures zlib-6
still beats LZN/LZNB on ratio, and Kraken/newLZ is not measured because no
licensed comparison oracle is configured in this repository yet.

## Tool Examples

```bash
./build/prosperopkg-inspect path/to/package.pkg
./build/prosperopkg-inspect --self-test
./build/prosperopkg-gp5 app_dir out.gp5 --flat --type app
./build/prosperopkg-fself input.elf output.self
./build/prosperopkg-keys UP9000-PPSA00000_00-PROSPERO00000000 00000000000000000000000000000000 000102030405060708090a0b0c0d0e0f
./build/prosperopkg-lzn compress input.bin output.lzn 2
./build/prosperopkg-lzn decompress output.lzn restored.bin
./build/prosperopkg-lzn bench input.bin 20 2
./build/prosperopkg-lzn block-compress input.bin output.lznb 2 524288
./build/prosperopkg-lzn block-info output.lznb
./build/prosperopkg-lzn block-decompress output.lznb restored.bin
./build/prosperopkg-lzn block-bench input.bin 20 2 524288
```

On Windows, use the executable paths generated by the selected CMake generator,
for example `build\Release\prosperopkg-inspect.exe`.

## Repository Layout

| Path | Description |
|---|---|
| `src/include/prosperopkg` | Public C++ headers. |
| `src/src` | Native library implementation. |
| `src/tools` | Command-line tools. |
| `src/tests` | Native regression tests. |
| `docs` | Current C++ documentation and format notes. |
| `legacy/csharp` | Archived C# implementation, old NativeAOT bridge, and old C# docs. |

## Documentation

- [docs/README.md](docs/README.md) - current documentation index.
- [docs/cpp-port-plan.md](docs/cpp-port-plan.md) - migration status and validation gates.
- [docs/kraken-clean-room.md](docs/kraken-clean-room.md) - clean-room plan for native Kraken/newLZ support.
- [docs/lzn-codec.md](docs/lzn-codec.md) - current LZN1 clean-room frame format and benchmark notes.
- [docs/ps5-pkg-format.md](docs/ps5-pkg-format.md) - PS5 package format notes.
- [reports/lzn-benchmark.md](reports/lzn-benchmark.md) - local LZN/LZNB/zlib benchmark report.
- [legacy/csharp/README.md](legacy/csharp/README.md) - archived C# reference notes.

## CI/CD

`.github/workflows/cmake.yml` builds with CMake + Ninja on Linux, macOS, and
Windows. The manual GitHub Actions dispatch accepts a release version, builds
size-optimized packages, uploads artifacts, and can create or update a GitHub
Release directly from the dashboard.

## License and Credits

LibProsperoPkg is distributed under GPL-3.0-or-later. See [LICENSE](LICENSE) and
[NOTICE](NOTICE).

- Original C# LibProsperoPkg: SvenGDK.
- C++ rewrite/port, native library, native tools, and native tests: seregonwar.
