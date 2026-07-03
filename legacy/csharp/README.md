# Legacy C# Reference

This directory contains the original managed LibProsperoPkg implementation and
its old NativeAOT bridge/workflows. It is kept as a parity reference for the C++
rewrite, not as the primary source tree.

Credits stay split clearly:

- Original C# LibProsperoPkg: SvenGDK.
- C++ rewrite/port and native tools: seregonwar.

## Layout

| Path | Description |
|---|---|
| `src/LibProsperoPkg` | Original C# project and embedded data. |
| `docs` | Original C# documentation. |
| `native-aot` | Old NativeAOT export bridge files. |
| `workflows` | Archived GitHub Actions workflows for the managed NativeAOT build. |

## Optional Build

```bash
dotnet build legacy/csharp/src/LibProsperoPkg/LibProsperoPkg.csproj -c Release
```

Use this only when comparing behavior against the C++ implementation or when
mining the old project for missing migration details.
