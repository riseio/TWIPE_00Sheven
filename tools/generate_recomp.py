#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

try:
    from .verify_rom import validate_rom
except ImportError:

    from verify_rom import validate_rom

def run(arguments: list[str], cwd: Path) -> None:
    subprocess.run(arguments, cwd=cwd, check=True)

def find_tool(build_dir: Path, name: str) -> Path:
    suffix = ".exe" if sys.platform == "win32" else ""
    candidates = (
        build_dir / "Release" / f"{name}{suffix}",
        build_dir / f"{name}{suffix}",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"built tool not found: {name} under {build_dir}")

def build_tool_targets(
    build_dir: Path,
    cwd: Path,
    runner=run,
) -> None:

    for target in ("N64RecompCLI", "RSPRecomp"):
        runner(
            [
                "cmake",
                "--build",
                str(build_dir),
                "--config",
                "Release",
                "--target",
                target,
                "--parallel",
                "1",
            ],
            cwd,
        )

def remove_generated(root: Path) -> None:
    generated = (root / "build" / "generated").resolve()
    expected_parent = (root / "build").resolve()
    if generated.parent != expected_parent or generated.name != "generated":
        raise RuntimeError(f"refusing to remove unexpected path: {generated}")
    if generated.exists():

        for entry in generated.iterdir():
            if entry.name == 'local-aot':
                continue
            if entry.is_symlink() or entry.is_file():
                entry.unlink()
            else:
                shutil.rmtree(entry)

def prepare_rom_configs(root: Path, rom: Path) -> dict[str, Path]:

    root = root.resolve()
    prepared = {}
    for name, paths in (
        ("twine.us.rev0.toml", {
            "rom_file_path": rom.resolve(),
            "symbols_file_path": root / "config/twine.us.rev0.syms.toml",
            "output_func_path": root / "build/generated/RecompiledFuncs",
        }),
        ("twine.audio.us.rev0.toml", {
            "rom_file_path": rom.resolve(),
            "output_file_path": root / "build/generated/rsp/twine_audio.cpp",
        }),
    ):
        text = (root / "config" / name).read_text(encoding="utf-8")
        for key, path in paths.items():
            replacement = key + " = " + json.dumps(path.as_posix(), ensure_ascii=False)
            text, count = re.subn(r"(?m)^" + key + r"\s*=\s*[^\n]+$",
                                 lambda _: replacement, text)
            if count != 1:
                raise ValueError(f"Expected one {key} in {name}; inspect generation config")
        prepared[name] = text
    directory = root / "build"
    directory.mkdir(exist_ok=True)

    directory = Path(tempfile.mkdtemp(prefix="generation-config-", dir=directory))
    result = {}
    for name, text in prepared.items():
        result[name] = directory / name
        result[name].write_text(text, encoding="utf-8")
    result["splat"] = directory / "rom-override.yaml"
    result["splat"].write_text(json.dumps({"options": {"target_path": rom.resolve().as_posix()}}),
                               encoding="utf-8")
    return result

def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--tools-preset", required=True)
    parser.add_argument("--runtime-preset", required=True)
    args = parser.parse_args()

    rom = args.rom.resolve()
    build_dir = args.build_dir.resolve()
    if root.resolve() not in build_dir.parents:
        raise ValueError("build directory must stay inside the project")
    validate_rom(rom)

    run(["cmake", "--preset", args.tools_preset], root)
    build_tool_targets(build_dir, root)

    configs = prepare_rom_configs(root, rom)
    remove_generated(root)
    run([sys.executable, "-m", "splat", "split", "twine.yaml", str(configs["splat"])], root)
    run([sys.executable, "tools/generate_symbols.py"], root)
    run(
        [str(find_tool(build_dir, "N64Recomp")), str(configs["twine.us.rev0.toml"])],
        root / "config",
    )
    run(
        [str(find_tool(build_dir, "RSPRecomp")), str(configs["twine.audio.us.rev0.toml"])],
        root / "config",
    )
    run([sys.executable, "tools/write_generated_manifest.py"], root)
    run(["cmake", "--preset", args.runtime_preset], root)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
