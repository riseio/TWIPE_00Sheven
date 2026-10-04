#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

FUNCTION_HEADER = re.compile(
    r"^nonmatching (?P<name>[A-Za-z_][A-Za-z0-9_]*), 0x(?P<size>[0-9A-Fa-f]+)"
    r"\s+^glabel (?P=name)\s+"
    r"^\s*/\* (?P<rom>[0-9A-Fa-f]+) (?P<vram>[0-9A-Fa-f]+) [0-9A-Fa-f]{8} \*/",
    re.MULTILINE,
)

@dataclass(frozen=True)
class Function:
    name: str
    rom: int
    vram: int
    size: int

@dataclass(frozen=True)
class Section:
    name: str
    rom: int
    vram: int
    size: int
    functions: tuple[Function, ...]

def parse_functions(path: Path) -> list[Function]:
    text = path.read_text(encoding="utf-8")
    expected = len(
        re.findall(
            r"^nonmatching [A-Za-z_][A-Za-z0-9_]*, 0x[0-9A-Fa-f]+\s+^glabel ",
            text,
            re.MULTILINE,
        )
    )
    functions = [
        Function(
            match["name"],
            int(match["rom"], 16),
            int(match["vram"], 16),
            int(match["size"], 16),
        )
        for match in FUNCTION_HEADER.finditer(text)
    ]
    if len(functions) != expected:
        raise ValueError(f"{path}: parsed {len(functions)} of {expected} functions")
    return functions

def segment_start(segment: object) -> int | None:
    if isinstance(segment, dict):
        value = segment.get("start")
    elif isinstance(segment, list) and segment:
        value = segment[0]
    else:
        return None
    return value if isinstance(value, int) else None

def subsegment_fields(subsegment: object) -> tuple[int | None, str | None]:
    if isinstance(subsegment, dict):
        return segment_start(subsegment), subsegment.get("type")
    if isinstance(subsegment, list) and len(subsegment) >= 2:
        return segment_start(subsegment), subsegment[1]
    return None, None

def load_sections(layout_path: Path, asm_dir: Path) -> list[Section]:
    try:
        import yaml
    except ImportError as error:
        raise RuntimeError("PyYAML is required; use the same environment as Splat") from error

    segments = yaml.safe_load(layout_path.read_text(encoding="utf-8"))["segments"]
    sections: list[Section] = []
    all_names: set[str] = set()

    for index, segment in enumerate(segments):
        if not isinstance(segment, dict) or segment.get("type") != "code":
            continue
        rom = segment_start(segment)
        vram = segment.get("vram")
        if not isinstance(rom, int) or not isinstance(vram, int):
            raise ValueError(f"{layout_path}: code segment needs integer ROM and VRAM")

        end = next(
            (
                start
                for later in segments[index + 1 :]
                if (start := segment_start(later)) is not None
            ),
            None,
        )
        if end is None or end <= rom:
            raise ValueError(f"{layout_path}: no valid end for {segment['name']}")

        subsegments = segment.get("subsegments", [])
        starts = [subsegment_fields(item)[0] for item in subsegments]
        functions: list[Function] = []
        for sub_index, subsegment in enumerate(subsegments):
            sub_start, sub_type = subsegment_fields(subsegment)
            if sub_type not in {"asm", "hasm"} or sub_start is None:
                continue
            sub_end = next(
                (start for start in starts[sub_index + 1 :] if start is not None),
                end,
            )
            asm_path = asm_dir / f"{sub_start:X}.s"
            for function in parse_functions(asm_path):
                mapped_rom = rom + function.vram - vram
                if function.rom != mapped_rom:
                    raise ValueError(
                        f"{function.name}: ROM 0x{function.rom:X} does not map "
                        f"from VRAM 0x{function.vram:X}"
                    )
                if (
                    function.size <= 0
                    or function.size % 4
                    or not sub_start <= function.rom
                    or function.rom + function.size > sub_end
                ):
                    raise ValueError(f"{function.name}: invalid or out-of-range size")
                if function.name in all_names:
                    raise ValueError(f"duplicate function name: {function.name}")
                all_names.add(function.name)
                functions.append(function)

        functions.sort(key=lambda function: function.rom)
        for previous, current in zip(functions, functions[1:]):
            if previous.rom + previous.size > current.rom:
                raise ValueError(f"{previous.name} overlaps {current.name}")
        if not functions:
            raise ValueError(f"{segment['name']}: no functions found")
        sections.append(
            Section(segment["name"], rom, vram, end - rom, tuple(functions))
        )

    if not any(
        function.vram == 0x80000400
        for section in sections
        for function in section.functions
    ):
        raise ValueError("entrypoint function 0x80000400 is missing")
    return sections

def render(sections: list[Section]) -> str:
    lines = [
        "# Generated by tools/generate_symbols.py; edit twine.yaml or Splat inputs.",
        "",
    ]
    for section in sections:
        lines.extend(
            [
                "[[section]]",
                f'name = "{section.name}"',
                f"rom = 0x{section.rom:X}",
                f"vram = 0x{section.vram:08X}",
                f"size = 0x{section.size:X}",
                "functions = [",
            ]
        )
        lines.extend(
            f'  {{ name = "{function.name}", vram = 0x{function.vram:08X}, '
            f"size = 0x{function.size:X} }},"
            for function in section.functions
        )
        if section.name == "main":

            if not any(function.name == "func_80069DCC" and
                       function.vram <= 0x8006A918 and
                       function.vram + function.size >= 0x8006A958
                       for function in section.functions):
                raise ValueError("Native ladder launch owner no longer contains the query block")
            lines.append('  { name = "twine_native_ladder_launch", '
                         'vram = 0x8006A918, size = 0x40 },')

            for name, owner, start, end in (
                ("twine_native_objectives", "func_80031028", 0x80031068, 0x80031144),
                ("twine_native_materials", "func_800336FC", 0x80033700, 0x80033FB8),
            ):
                if not any(function.name == owner and function.vram < start and
                           function.vram + function.size >= end for function in section.functions):
                    raise ValueError(f"Native query entry is outside {owner}")
                lines.append(f'  {{ name = "{name}", vram = 0x{start:08X}, size = 0x{end-start:X} }},')
        lines.extend(["]", ""])
    return "\n".join(lines)

def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", type=Path, default=root / "twine.yaml")
    parser.add_argument(
        "--asm", type=Path, default=root / "build" / "generated" / "asm"
    )
    parser.add_argument(
        "--output", type=Path, default=root / "config" / "twine.us.rev0.syms.toml"
    )
    args = parser.parse_args()

    sections = load_sections(args.layout, args.asm)
    output = render(sections)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(output, encoding="utf-8", newline="\n")
    temporary.replace(args.output)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
