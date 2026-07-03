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
- Native source-folder inner image and CNT/FIH package builders exposed through
  the legacy C ABI.
- PS5 image digest helpers for package/body/rollup/entry/fixed-info/sblock data.
- UCP archive build/read/verify/repair support.
- SELF parsing and ELF-to-fake-SELF generation.
- GP5 project model, XML serialization, and folder-to-GP5 generation.
- CLI tools for inspection, fake-self generation, GP5 generation, and key
  derivation.
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
| `is_valid_content_id` | 0.271 us/call | 0.145 us/call | 1.87x faster |
| `compose_content_id` | 1.009 us/call | 0.715 us/call | 1.41x faster |
| `is_elf` | 0.273 us/call | 0.277 us/call | 0.99x |
| `make_fself` | 2.226 us/call | 1.584 us/call | 1.41x faster |
| Shared library size | 6,064,504 bytes | 164,392 bytes | 36.89x smaller |
| Baseline bundle vs C++ lib+tools | 34,378,430 bytes | 395,880 bytes | 86.84x smaller |

Correctness currently matches for the checked C ABI surfaces, including
content/title identifiers, fake-SELF generation, package type detection,
inner-PFS encryption, C++ PFSC zlib pack/unpack, PFSv3 stored pack/unpack,
native inner-image building, and native CNT/FIH package building. The local C#
baseline cannot pack PFSC on this host because its PFSv3 compression path
reports missing SHA3-256 support; the C++ PFSC paths still round-trip
successfully.

## Tool Examples

```bash
./build/prosperopkg-inspect path/to/package.pkg
./build/prosperopkg-inspect --self-test
./build/prosperopkg-gp5 app_dir out.gp5 --flat --type app
./build/prosperopkg-fself input.elf output.self
./build/prosperopkg-keys UP9000-PPSA00000_00-PROSPERO00000000 00000000000000000000000000000000 000102030405060708090a0b0c0d0e0f
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
- [docs/ps5-pkg-format.md](docs/ps5-pkg-format.md) - PS5 package format notes.
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
