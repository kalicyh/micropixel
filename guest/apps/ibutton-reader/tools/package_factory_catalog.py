#!/usr/bin/env python3
"""Build a local factory App Bundle with a private DS1977 data catalog."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


APP_DIR = Path(__file__).resolve().parents[1]
MICROPIXEL_ROOT = APP_DIR.parents[2]
GENERATED = APP_DIR / "factory_catalog.generated.hpp"


def cpp_bytes(data: bytes) -> str:
    rows = []
    for start in range(0, len(data), 16):
        rows.append("        " + ", ".join(f"0x{value:02X}" for value in data[start : start + 16]))
    return ",\n".join(rows)


def generate(source: Path) -> int:
    entries = []
    for path in sorted(source.glob("*.bin"), key=lambda item: item.name.casefold()):
        payload = path.read_bytes()
        if len(payload) != 4112:
            raise ValueError(f"{path.name}: expected 16-byte header + 4096-byte image")
        if len(set(payload[:16])) != 1 or payload[0] not in (0x00, 0x55, 0xFF):
            raise ValueError(f"{path.name}: unsupported 16-byte file header")
        entries.append((path.name, payload[16:]))
    if not entries:
        raise ValueError(f"No .bin files found in {source}")

    lines = [
        "#ifndef MICROPIXEL_IBUTTON_READER_FACTORY_CATALOG_GENERATED_HPP",
        "#define MICROPIXEL_IBUTTON_READER_FACTORY_CATALOG_GENERATED_HPP",
        "#include <array>",
        "#include <cstdint>",
        "namespace ibutton_reader {",
        "struct FactoryDataset final { const char* name; std::array<uint8_t, 4096> bytes; };",
        f"inline constexpr std::array<FactoryDataset, {len(entries)}> kFactoryCatalog{{{{",
    ]
    for name, payload in entries:
        lines.extend([f"    {{{json.dumps(name, ensure_ascii=False)}, {{{{", cpp_bytes(payload), "    }}},"])
    lines.extend(["}};", "}  // namespace ibutton_reader", "#endif", ""])
    GENERATED.write_text("\n".join(lines), encoding="utf-8")
    return len(entries)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path, help="folder containing the factory .bin datasets")
    parser.add_argument("--output-dir", type=Path, default=Path("build/ibutton-reader-factory"))
    parser.add_argument("--profile", choices=("development", "release", "size", "performance"), default="release")
    parser.add_argument("--aot-target", choices=("riscv32-ilp32f", "xtensa"), default="riscv32-ilp32f")
    args = parser.parse_args()

    try:
        count = generate(args.source_dir.expanduser().resolve())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    command = [
        sys.executable,
        str(MICROPIXEL_ROOT / "tools/micropixel"),
        "package",
        str(APP_DIR),
        "--profile",
        args.profile,
        "--aot-target",
        args.aot_target,
        "--output-dir",
        str(args.output_dir),
        "--force",
    ]
    print(f"Packaging private factory catalog: {count} datasets (data is embedded in the bundle).", flush=True)
    return subprocess.run(command, cwd=MICROPIXEL_ROOT, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
