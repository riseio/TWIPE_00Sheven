#!/bin/sh
set -eu

test "$#" -eq 1 || { printf 'Usage: %s /path/to/rom\n' "$0" >&2; exit 1; }
rom=$(realpath -- "$1")
project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project"
python="$project/build/python-linux/bin/python"
if test ! -x "$python"; then
    python3 -m venv "$project/build/python-linux"
fi
"$python" -m pip install -r requirements.txt
"$python" tools/generate_recomp.py --rom "$rom" \
    --build-dir "$project/build/linux" \
    --tools-preset linux-tools --runtime-preset linux-release
cmake --build --preset linux-release
