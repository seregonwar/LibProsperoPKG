# LibProsperoPkg Documentation

This folder now tracks the active C++ rewrite. C#-specific docs were moved to
`../legacy/csharp/docs` together with the original implementation so the root
documentation can stay focused on the native codebase.

A native ImGui frontend is in planning, but the current shipped surface remains
the C++ library, the legacy-compatible C ABI, and CLI tools.

## Current Documents

| Document | Description |
|---|---|
| [cpp-port-plan.md](cpp-port-plan.md) | Current C++ migration status, implemented native surface, and validation gates. |
| [ps5-pkg-format.md](ps5-pkg-format.md) | Technical notes on the PS5 package format and the creation process. |
| [native comparison report](../reports/native-comparison.md) | Latest local C# NativeAOT vs C++ C ABI correctness, performance, and size report. |

## Legacy C# Reference

| Document | Description |
|---|---|
| [legacy getting-started](../legacy/csharp/docs/getting-started.md) | Original C# build and first-package guide. |
| [legacy API overview](../legacy/csharp/docs/api-overview.md) | Original C# public API notes by namespace. |
| [legacy implementation status](../legacy/csharp/docs/implementation-status.md) | Original managed implementation status. |

The legacy documents are useful for parity checks, but new implementation work
should target the C++ library, tools, tests, and CMake workflow in the repository
root.
