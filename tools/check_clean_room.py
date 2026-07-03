#!/usr/bin/env python3
#
# LibProsperoPkg - A library for building and inspecting PS5 packages.
# Copyright (C) 2026 seregonwar.

from __future__ import annotations

import argparse
from pathlib import Path


FORBIDDEN = [
    "powzix",
    "ooz",
    "KrakenBitReader",
    "KrakenBitWriter",
    "KrakenDecoder",
    "OodleKrakenEncoder",
]


def iter_text_files(root: Path) -> list[Path]:
    suffixes = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".py", ".txt", ".md"}
    return [
        path
        for path in root.rglob("*")
        if path.is_file() and path.suffix in suffixes
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description="Reject unlicensed ooz/legacy Kraken imports in active C++ sources.")
    parser.add_argument("paths", nargs="+", type=Path)
    args = parser.parse_args()

    violations: list[str] = []
    for root in args.paths:
        for path in iter_text_files(root):
            text = path.read_text(encoding="utf-8", errors="ignore")
            lowered = text.lower()
            for token in FORBIDDEN:
                haystack = lowered if token.islower() else text
                needle = token if not token.islower() else token.lower()
                if needle in haystack:
                    violations.append(f"{path}:{token}")

    if violations:
        print("Clean-room guard rejected forbidden references:")
        for item in violations:
            print(f"  {item}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
