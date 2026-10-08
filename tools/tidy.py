#!/usr/bin/env python3
"""Runs the pinned clang-tidy on Lodestone's C++ code, using a build tree's compilation database.

Usage:
    python tools/tidy.py [--build-dir build/debug] [--jobs N] [file ...]

The build tree only needs to be configured (cmake --preset debug), not built. With no files given, every Lodestone
source file in the compilation database is checked; headers are checked through the sources that include them.
Exits with 1 if clang-tidy reports anything - .clang-tidy treats every warning as an error.
"""

import argparse
import json
import os
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from pinned_tools import REPOSITORY_ROOT, find_pinned_tool

SOURCE_DIRECTORIES = ("Source", "Tests", "Samples")


def is_lodestone_source(path: Path) -> bool:
    try:
        relative = path.resolve().relative_to(REPOSITORY_ROOT)
    except ValueError:
        return False
    return len(relative.parts) > 1 and relative.parts[0] in SOURCE_DIRECTORIES


def load_compilation_database(build_directory: Path) -> list[dict]:
    database_path = build_directory / "compile_commands.json"
    if not database_path.is_file():
        sys.exit(f"error: {database_path} not found. Configure the build first, e.g. cmake --preset debug")
    return json.loads(database_path.read_text(encoding="utf-8"))


def uses_msvc(entries: list[dict]) -> bool:
    """Whether the build tree compiles with MSVC (every entry uses the same compiler)."""
    if not entries:
        return False
    # posix=False keeps Windows paths intact; quotes around a path with spaces are stripped below
    arguments = entries[0].get("arguments") or shlex.split(entries[0].get("command", ""), posix=False)
    compiler = Path(arguments[0].strip('"')).name.lower() if arguments else ""
    return compiler in ("cl.exe", "cl", "clang-cl.exe", "clang-cl")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path, default=REPOSITORY_ROOT / "build" / "debug",
                        help="configured build tree with compile_commands.json (default: build/debug)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="parallel clang-tidy processes")
    parser.add_argument("files", nargs="*", type=Path, help="source files to check (default: all)")
    arguments = parser.parse_args()

    clang_tidy = find_pinned_tool("clang-tidy")
    build_directory = arguments.build_dir.resolve()
    entries = load_compilation_database(build_directory)

    database_files = {Path(entry["file"]).resolve() for entry in entries}
    if arguments.files:
        files = sorted(path.resolve() for path in arguments.files)
        missing = [path for path in files if path not in database_files]
        if missing:
            sys.exit("error: not in the compilation database: " + ", ".join(str(path) for path in missing))
    else:
        files = sorted(path for path in database_files if is_lodestone_source(path))
    if not files:
        sys.exit("error: no Lodestone source files in the compilation database")

    command = [clang_tidy, "-p", str(build_directory), "--quiet"]
    if uses_msvc(entries):
        # clang-tidy parses MSVC command lines as clang-cl, which ignores some MSVC-only flags
        command.append("--extra-arg=-Wno-unused-command-line-argument")

    def run(path: Path) -> subprocess.CompletedProcess:
        return subprocess.run([*command, str(path)], cwd=REPOSITORY_ROOT, capture_output=True, text=True)

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as executor:
        for path, result in zip(files, executor.map(run, files)):
            if result.returncode != 0:
                failures += 1
                print(f"--- {path.relative_to(REPOSITORY_ROOT)}")
                print(result.stdout, end="")
                print(result.stderr, end="", file=sys.stderr)

    if failures:
        print(f"\nclang-tidy reported problems in {failures} of {len(files)} files", file=sys.stderr)
        return 1

    print(f"clang-tidy found no problems in {len(files)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
