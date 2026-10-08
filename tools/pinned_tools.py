"""Finds the code style tools at the versions pinned in tools/requirements.txt (shared by format.py and tidy.py)."""

import shutil
import subprocess
import sys
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
REQUIREMENTS = REPOSITORY_ROOT / "tools" / "requirements.txt"


def get_pinned_version(tool: str) -> str:
    """Returns the version tools/requirements.txt pins for a tool, such as "clang-format"."""
    for line in REQUIREMENTS.read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{tool}=="):
            return line.split("==", 1)[1].strip()
    sys.exit(f"error: {REQUIREMENTS} doesn't pin {tool}")


def find_pinned_tool(tool: str) -> str:
    """Returns the path of a tool on the PATH, exiting with an error unless it's the pinned version."""
    pinned_version = get_pinned_version(tool)
    executable = shutil.which(tool)
    if executable is None:
        sys.exit(f"error: {tool} not found. Install it with: pip install -r tools/requirements.txt")

    output = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True).stdout
    if f"version {pinned_version}" not in output:
        sys.exit(
            f"error: {executable} is not {tool} {pinned_version} ({output.strip()}).\n"
            "Install the pinned version with: pip install -r tools/requirements.txt"
        )
    return executable
