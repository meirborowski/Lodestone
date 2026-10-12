#!/usr/bin/env python3
"""Prints a benchmark report (LodestoneBenchmark --output) as Markdown, for CI's job summary.

Usage:
    python tools/benchmark-summary.py <report.json> [<title>]

CI reports the benchmark's CPU frame time but doesn't gate it - shared runners are too noisy (see
docs/Testing.md#performance). Draw calls are gated by the "Benchmark draw calls stay within the baseline" test.
"""

import json
import pathlib
import sys


def milliseconds(summary):
    return "n/a" if not summary else f"{summary['mean']:.2f} ms (p95 {summary['p95']:.2f} ms)"


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print(__doc__, file=sys.stderr)
        return 2
    report = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
    title = sys.argv[2] if len(sys.argv) == 3 else "Benchmark"
    width, height = report["size"]
    print(f"### {title}")
    print()
    print(f"{report['frames']} frames at {width}x{height} on {report['device']} ({report['driver']})")
    print()
    print("| Measure | Value |")
    print("|---|---|")
    print(f"| CPU frame time | {milliseconds(report['cpuMilliseconds'])} |")
    print(f"| GPU frame time | {milliseconds(report.get('gpuMilliseconds'))} |")
    print(f"| Draw calls ({report['probeFrames']} probe frames) | {report['drawCalls']} |")
    print(f"| Triangles ({report['probeFrames']} probe frames) | {report['triangles']} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
