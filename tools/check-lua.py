#!/usr/bin/env python3
"""Type-checks projects' Lua scripts with lua-language-server, against the annotations the engine generates.

Usage:
    python tools/check-lua.py --luals <lua-language-server> --work-dir <directory> [--expect-marked] <project>...

Each project is checked as lua-language-server's workspace, with its own .luarc.json and annotations
(.lodestone/Lodestone.lua, which the editor writes - see docs/Decisions/0022-scripting.md). Every warning and error is
printed as <file>:<line>:<column>, and any fails the check. The work directory takes lua-language-server's logs and
reports. CMake fetches lua-language-server and runs this as the "Scripting.Scripts type-check" tests.

With --expect-marked, the check passes when the problems are exactly those marked in the scripts: a line ending in
"-- expect: <code>" (or several codes, separated by commas) must have those problems and no others. That's how the
check itself is tested.
"""

import argparse
import dataclasses
import json
import pathlib
import re
import subprocess
import sys
import urllib.parse
import urllib.request

SEVERITIES = {1: "error", 2: "warning", 3: "information", 4: "hint"}
EXPECT = re.compile(r"--\s*expect:\s*([\w-]+(?:\s*,\s*[\w-]+)*)\s*$")


@dataclasses.dataclass(frozen=True, order=True)
class Problem:
    path: str
    line: int
    column: int
    code: str
    severity: str
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}:{self.column}: {self.severity}: {self.message} [{self.code}]"


def path_from_uri(uri: str) -> pathlib.Path:
    return pathlib.Path(urllib.request.url2pathname(urllib.parse.unquote(urllib.parse.urlparse(uri).path)))


def run(luals: pathlib.Path, project: pathlib.Path, work: pathlib.Path) -> list[Problem] | None:
    """The problems lua-language-server finds in a project, or None if it failed."""
    report = work / f"{project.name}.json"
    report.unlink(missing_ok=True)
    result = subprocess.run(
        [
            str(luals),
            f"--check={project.as_posix()}",
            f"--configpath={(project / '.luarc.json').as_posix()}",
            "--checklevel=Warning",
            "--check_format=json",
            f"--check_out_path={report.as_posix()}",
            f"--logpath={(work / 'logs').as_posix()}",
            f"--metapath={(work / 'meta').as_posix()}",
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    # Problems by file URI; without any, an empty list
    found = (json.loads(report.read_text(encoding="utf-8")) if report.exists() else None) or {}
    problems = []
    for uri, diagnostics in found.items():
        path = path_from_uri(uri)
        shown = path.relative_to(project) if path.is_relative_to(project) else path
        for diagnostic in diagnostics:
            start = diagnostic["range"]["start"]
            problems.append(Problem(
                path=f"{project.name}/{shown.as_posix()}",
                line=start["line"] + 1,
                column=start["character"] + 1,
                code=diagnostic.get("code", ""),
                severity=SEVERITIES.get(diagnostic.get("severity"), "problem"),
                message=diagnostic["message"].splitlines()[0],
            ))
    if result.returncode != 0 and not problems:
        print(f"lua-language-server failed on {project} (exit code {result.returncode}):", file=sys.stderr)
        print(result.stdout + result.stderr, file=sys.stderr)
        return None
    return sorted(problems)


def marked(project: pathlib.Path) -> set[tuple[str, int, str]]:
    """The problems the project's scripts are marked to have: (path, line, code)."""
    expected = set()
    for script in sorted(project.rglob("*.lua")):
        if script.relative_to(project).parts[0] == ".lodestone":
            continue
        path = f"{project.name}/{script.relative_to(project).as_posix()}"
        for number, line in enumerate(script.read_text(encoding="utf-8").splitlines(), start=1):
            if match := EXPECT.search(line):
                expected.update((path, number, code.strip()) for code in match.group(1).split(","))
    return expected


def check(luals: pathlib.Path, project: pathlib.Path, work: pathlib.Path, expect_marked: bool) -> bool:
    problems = run(luals, project, work)
    if problems is None:
        return False
    if not expect_marked:
        for problem in problems:
            print(problem)
        count = len(problems)
        print(f"{project.name}: {count or 'no'} problem{'' if count == 1 else 's'}")
        return count == 0

    expected = marked(project)
    if not expected:
        print(f"{project.name} has no scripts marked with the problems they should have", file=sys.stderr)
        return False
    found = {(problem.path, problem.line, problem.code) for problem in problems}
    for problem in problems:
        if (problem.path, problem.line, problem.code) not in expected:
            print(f"Unexpected: {problem}")
    for path, line, code in sorted(expected - found):
        print(f"Not found: {path}:{line}: [{code}]")
    print(f"{project.name}: {len(found & expected)} of {len(expected)} marked problems found, "
          f"{len(found - expected)} unexpected")
    return found == expected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--luals", type=pathlib.Path, required=True, help="the lua-language-server executable")
    parser.add_argument("--work-dir", type=pathlib.Path, required=True, help="where logs and reports go")
    parser.add_argument("--expect-marked", action="store_true",
                        help="pass when the problems are exactly those marked in the scripts")
    parser.add_argument("projects", type=pathlib.Path, nargs="+", help="project directories to check")
    arguments = parser.parse_args()

    work = arguments.work_dir.resolve()
    projects = [path.resolve() for path in arguments.projects]
    # lua-language-server takes "--" anywhere in an argument as the start of another option
    for path in [work, *projects]:
        if "--" in path.as_posix():
            print(f"lua-language-server can't be given a path containing '--': {path}", file=sys.stderr)
            return 2
    work.mkdir(parents=True, exist_ok=True)

    passed = True
    for project in projects:
        if not (project / ".luarc.json").is_file():
            print(f"{project} has no .luarc.json: opening it in the editor writes one", file=sys.stderr)
            passed = False
        elif not check(arguments.luals, project, work, arguments.expect_marked):
            passed = False
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
