# Kraken Clean-Room Plan

LibProsperoPkg needs native PFSv3 Kraken/newLZ support, but the known public
`powzix/ooz` repository does not currently publish a clear license notice. The
archived C# implementation under `legacy/csharp` also appears to have been
informed by that project. For the active C++ rewrite, treat both as historical
context only: do not copy, mechanically translate, vendor, or adapt their code
into `src/`.

## Allowed Inputs

- Independently generated compressed/uncompressed fixtures.
- Public, non-code descriptions of Oodle Data concepts and PFSv3 container
  fields.
- Black-box behavior from legally usable tools or binaries supplied by the
  project owner.
- The existing native PFSv3 container writer/reader, SHA3 digest checks, and
  boundary-table tests already implemented in this repository.

## Disallowed Inputs

- Source code from `powzix/ooz` until a compatible license is confirmed.
- Mechanical translation of `legacy/csharp/src/LibProsperoPkg/PFS/Compression/Oodle`.
- Copying table layouts, control flow, magic decoding tables, or bitstream
  routines from unlicensed reverse-engineering code.

## Implementation Milestones

1. Preserve the current PFSv3 stored-block path as the safe fallback.
2. Add a native Kraken block codec namespace with tests driven by fixtures, not
   by copied implementation code.
3. Decode one-chunk and two-chunk PFSv3 block framing, including digest
   validation and exact output sizing.
4. Implement a conservative encoder that emits Kraken-compatible compressed
   blocks only when its own decoder and an independent fixture oracle verify the
   round-trip; otherwise keep the stored-block fallback.
5. Expand the package builder so `InnerCompression::kraken` chooses compressed
   PFSv3 blocks opportunistically and records stored blocks for incompressible
   data.
6. Only claim byte parity after fixture-based comparisons against known-good
   packages pass on all CI platforms.

## Current Status

The active C++ implementation already emits valid PFSv3 stored containers with
the expected section directory, SHA3 block/file digests, boundary table, and
exact-size unpacking. It also now includes LZN1 plus the LZNB indexed block
archive, independent clean-room codec work used to evolve compression
infrastructure without touching unlicensed `ooz` or legacy Oodle code. LZN is
not Kraken-compatible yet; full Kraken newLZ compression/decompression remains
a clean-room implementation task beyond the stored-block fallback.
