# C++ Port Plan

This repository now carries the native C++ port/rewrite under `src/`. The original C# library by
SvenGDK has been moved to `legacy/csharp` and remains the parity reference until the C++
implementation covers the same reader, writer, packaging, compression, crypto, and tooling surface.
The C++ port/rewrite and native tooling are credited to seregonwar.

## Goals

- Build on Windows, Linux, and macOS from one CMake project.
- Keep the native public API small, explicit, and exception-safe.
- Preserve package bytes and parser behavior against the legacy C# implementation.
- Prefer standard C++ and small, auditable dependencies. Crypto and image codecs should be wired
  through cross-platform packages rather than platform-specific shell-outs.
- Design an optional ImGui frontend for inspection/build workflows. No GUI is shipped yet; the
  frontend is in planning and must stay separate from the small native library/runtime surface.

## Current Native Slice

- `prosperopkg_cpp` static library.
- CNT/FIH package type detection.
- CNT header and entry-table reader, including embedded CNT parsing from debug/retail FIH images.
- CNT writer with generated `ENTRY_NAMES` table.
- Content-id and title-id helpers.
- CRC-32C Castagnoli reducer.
- UCP archive build/read/validate/digest-repair support with an in-process SHA-1 implementation.
- SELF detection, structural validation, segment-table parsing, ELF-header extraction, and ext-info
  parsing.
- SHA-256, SHA3-256, and HMAC-SHA256 helpers.
- PFS key derivation: SHA3 EKPFS, classic/new-crypt tweak/data keys, and image sign key.
- AES-128 block primitive and AES-XTS data-unit transform.
- Inner PFS image AES-XTS transform helper that leaves the header block plaintext.
- Legacy C ABI inner-PFS encryption that patches the encrypted-mode bit and matches the C#
  NativeAOT baseline byte-for-byte when the input image already carries a deterministic seed.
- Raw PFSC support, zlib PFSC pack/unpack support when zlib is available, and PFSv3 stored
  containers with SHA3 file/block digests for the Kraken-compatible inner-image path.
- Native source-folder inner-image builder with deterministic PS5 superblock, file index, AES-XTS
  encrypted form, PFSC zlib wrapped form, and PFSv3 stored wrapped form.
- Native CNT/FIH package builder that emits parser-readable metadata containers and debug FIH
  images from a source folder through the legacy C ABI.
- Outer finalized-image PFS AES-XTS transform helper with data/signed/plaintext block
  classification and signed-block sector flag support.
- Outer-PFS signing/integrity helpers: per-block SHA3 hash, superblock ICV computation/writer, and
  super-root inode `{hash, blockIndex}` stamping.
- PS5 CNT/finalized-image digest helpers: sblock/game/fixed-info/body/entry/package/rollup
  digests, content/header digest preimages, digest-table generation, FIH-relative mount descriptor
  patching, and plaintext superblock lookup.
- Fake-self generation from 64-bit ELF input.
- GP5 project model, normal/flat XML serialization, folder creators, and a GP5 generation tool.
- Legacy-compatible `LibProsperoPkg` C ABI shared library exposing the same native export names as
  the C# NativeAOT baseline for the ported surfaces.
- `psxpkg.h` kept as an optional GPL-3.0-or-later reference reader authored by seregonwar. It is
  not linked into the native CMake library or tools unless a concrete need appears.
- `prosperopkg-inspect` CLI tool for PKG/CNT/FIH, UCP, and SELF inspection, including
  CNT/FIH digest reporting for package/body/rollup/entry-table/fixed-info/sblock values when
  those regions are available.
- `prosperopkg-fself` CLI tool for ELF-to-fake-SELF conversion.
- `prosperopkg-gp5` CLI tool for folder-to-GP5 project generation.
- `prosperopkg-keys` CLI tool for deriving EKPFS and PFS image keys from content-id/passcode/seed.
- Native tests for reader/writer round-trip, FIH embedded CNT parsing, UCP, SELF, CRC-32C,
  SHA-256, SHA3-256, HMAC-SHA256, AES-128, AES-XTS, PFS key derivation, PFS image
  transforms, outer-PFS signature helpers, CNT/finalized-image digest helpers, GP5, content-id
  helpers, inspect-tool self-tests, inspect-tool digest output, fake-self tool generation, GP5 tool
  generation, key-derivation tool output, and CLI argument validation.
- Optional CTest/Python comparison against a C# NativeAOT baseline directory, covering exported C
  ABI functions, parity-sensitive results, inner-PFS encryption, PFSC zlib/PFSv3 round-trips,
  microbenchmarks, and binary-size totals.

## Parity Status

The native tests verify parity-sensitive behavior for the C++ surfaces that have already been
ported: identifiers, package container round-trips, embedded CNT detection, digest math, UCP,
SELF/fake-SELF, GP5 serialization, key derivation, AES-XTS transforms, PFS signing helpers, and
tool output. The comparison harness also verifies the legacy C ABI against a C# NativeAOT baseline
when `LIBPROSPEROPKG_COMPARE_BASELINE` points at a baseline folder. Full C# parity is not declared
yet because the remaining managed-only surfaces still need native equivalents and fixture
comparisons.

Latest local comparison against `test/libprosperopkg-osx-arm64`:

- Exported C ABI functions: 15/15 matched.
- Correctness checks: all checked ported API behavior matched the C# NativeAOT baseline.
- Size: C++ shared library plus tools was 395,880 bytes vs 34,378,430 bytes for the C# baseline
  folder, making the native bundle 86.84x smaller.
- Performance: C++ was faster for content-id validation 1.87x, content-id composition 1.41x, and
  fake-SELF generation 1.41x. The tiny ELF probe measured essentially at parity on this host
  (0.99x).
- Inner-PFS encryption matched the C# NativeAOT baseline byte-for-byte on the deterministic seeded
  fixture.
- C++ PFSC zlib pack/unpack round-tripped 131,072 bytes. C++ PFSv3 stored pack/unpack
  round-tripped an unaligned 266,273-byte payload with format version 3. The local C# baseline's
  PFSC pack path returned `SHA3-256 is required for the PS5 PFSv3 compression format but is not
  available on this host`, so C# compressed PFSC parity still needs a host/tooling fixture that can
  exercise that path.
- `lpp_build_inner_image` and `lpp_build_package` now have native implementations and pass C ABI
  smoke checks. The current builder output is deterministic and parser-readable, but not yet
  byte-identical to the legacy C# PfsBuilder/ProsperoPkgBuilder output.

Known parity gaps:

- End-to-end package builder byte parity against legacy C# fixtures.
- Full kernel-mountable PFS layout/tree generation parity.
- Full Kraken newLZ compression/decompression parity for PFSv3 blocks beyond the native stored-block
  path.
- RSA metadata signing and encrypted-entry package helpers.
- DDS/texture generation parity.
- Fixture-based C++ vs C# byte comparisons once the legacy C# build is available locally or in CI.

## Migration Order

1. Container primitives: reader, writer, FIH wrapper, entry digests, comparison tools.
2. PFS layout and filesystem tree generation.
3. Remaining crypto: RSA metadata signing and any package-specific AES/RSA helpers still used by
   encrypted entries.
4. Full PFSv3 Kraken decoder and encoder beyond the stored-block path.
5. End-to-end package builder parity, with fixture-driven C++ vs C# byte/field comparisons.
6. Planned optional ImGui frontend for inspection/build workflows, kept separate from the core
   native library.

## Validation Gates

- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- `python3 tools/compare_native_parity.py --baseline-dir test/libprosperopkg-osx-arm64 --cpp-lib build-ninja-release/LibProsperoPkg.dylib`
- Fixture comparisons against known CNT/FIH/PFS samples.
- Cross-platform CI matrix for Windows, Linux, and macOS.
