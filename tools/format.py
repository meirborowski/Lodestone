#!/usr/bin/env python3
"""Checks or fixes the formatting of Lodestone's C++ code with the pinned clang-format.

Usage:
    python tools/format.py           Check formatting; exits with 1 and lists the files that need formatting
    python tools/format.py --fix     Format the files in place

Only files Git tracks or would track (not ignored) are formatted, so build trees and fetched dependencies are never
touched.
"""

import argparse
import subprocess
import sys

from pinned_tools import REPOSITORY_ROOT, find_pinned_tool

SOURCE_DIRECTORIES = ("Source", "Tests", "Samples")
SOURCE_EXTENSIONS = (".h", ".hpp", ".inl", ".cpp")
# Files per clang-format invocation, keeping command lines well within the Windows limit
BATCH_SIZE = 100


def get_source_files() -> list[str]:
    output = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *SOURCE_DIRECTORIES],
        cwd=REPOSITORY_ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    files = {path for path in output.splitlines() if path.endswith(SOURCE_EXTENSIONS)}
    # Skip files deleted from the working tree but still in the index
    return sorted(path for path in files if (REPOSITORY_ROOT / path).is_file())


def run_in_batches(command: list[str], files: list[str]) -> list[subprocess.CompletedProcess]:
    results = []
    for start in range(0, len(files), BATCH_SIZE):
        batch = files[start : start + BATCH_SIZE]
        results.append(subprocess.run([*command, *batch], cwd=REPOSITORY_ROOT, capture_output=True, text=True))
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="format the files in place instead of checking them")
    arguments = parser.parse_args()

    clang_format = find_pinned_tool("clang-format")
    files = get_source_files()
    if not files:
        print("No source files to format")
        return 0

    if arguments.fix:
        results = run_in_batches([clang_format, "-i", "--style=file"], files)
        failed = [result for result in results if result.returncode != 0]
        for result in failed:
            print(result.stderr, end="", file=sys.stderr)
        if failed:
            return 1
        print(f"Formatted {len(files)} files")
        return 0

    results = run_in_batches([clang_format, "--dry-run", "--Werror", "--style=file"], files)
    failed = [result for result in results if result.returncode != 0]
    if failed:
        for result in failed:
            print(result.stderr, end="", file=sys.stderr)
        print("\nSome files aren't formatted. Fix them with: python tools/format.py --fix", file=sys.stderr)
        return 1

    print(f"All {len(files)} files are formatted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
