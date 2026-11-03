"""Sorts a fuzz run's findings into this repository's and libdivecomputer's.

    python3 .github/scripts/fuzz_triage.py FUZZER FINDINGS_DIR REPOSITORY_ROOT

Re-runs every crash, timeout, leak and out-of-memory input the fuzzer kept, reads the
sanitizer's report, and attributes each to the first stack frame that is in the repository:
under vendor/libdivecomputer it is libdivecomputer's, anywhere else it is this repository's.
Writes a Markdown summary to $GITHUB_STEP_SUMMARY (or standard output) grouped by where each
finding is, and exits non-zero when any finding is this repository's. libdivecomputer's are
listed, not failed: the vendored copy stays what its upstream released, and README.md says
how a caller contains its parsers.
"""

import collections
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

FRAME = re.compile(r"^\s*#\d+ 0x[0-9a-f]+ in (\S+) (/\S+?):(\d+)(?::\d+)?\s*$")
UBSAN = re.compile(r"^(/\S+?):(\d+):\d+: runtime error: (.*)$")
KIND = re.compile(r"ERROR: (?:\w+Sanitizer|libFuzzer): (.+?)(?: on | after | \(|$)")
# Byte readers every parser calls: the finding is its caller's.
HELPERS = ("vendor/libdivecomputer/src/array.c",)


def attribute(fuzzer, path, root):
    """(kind, function, file:line, owner) for one input; owner is ours, vendor or none."""
    try:
        run = subprocess.run(
            [fuzzer, "-timeout=10", "-rss_limit_mb=2048", "-malloc_limit_mb=1024", str(path)],
            capture_output=True, text=True, errors="replace", timeout=120,
        )
        output = run.stdout + run.stderr
    except subprocess.TimeoutExpired:
        return ("hang past the re-run's limit", "?", "?", "none")
    if run.returncode == 0:
        return ("did not reproduce", "?", "?", "none")

    vendor = str(root / "vendor" / "libdivecomputer") + "/"
    kind = None
    for line in output.splitlines():
        ub = UBSAN.match(line)
        if ub and ub.group(1).startswith(str(root) + "/"):
            owner = "vendor" if ub.group(1).startswith(vendor) else "ours"
            where = f"{os.path.relpath(ub.group(1), root)}:{ub.group(2)}"
            message = re.sub(r"-?\d+", "N", ub.group(3))
            return (f"undefined behaviour: {message}", "?", where, owner)
        if kind is None:
            found = KIND.search(line)
            if found:
                kind = found.group(1)
        frame = FRAME.match(line)
        if kind is not None and frame and frame.group(2).startswith(str(root) + "/"):
            if os.path.relpath(frame.group(2), root) in HELPERS:
                continue
            owner = "vendor" if frame.group(2).startswith(vendor) else "ours"
            where = f"{os.path.relpath(frame.group(2), root)}:{frame.group(3)}"
            return (kind, frame.group(1), where, owner)
    return (kind or f"exit {run.returncode}", "?", "?", "none")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    fuzzer, findings, root = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]).resolve()
    inputs = sorted(p for p in findings.iterdir() if p.is_file()) if findings.is_dir() else []

    groups = collections.defaultdict(list)
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
        for path, result in zip(inputs, pool.map(lambda p: attribute(fuzzer, p, root), inputs)):
            groups[result].append(path.name)

    titles = {
        "ours": "In this repository's code — these fail the job",
        "vendor": "In the vendored libdivecomputer — listed, not failed",
        "none": "Not attributed to a frame in the repository",
    }
    lines = [f"## Fuzz findings: {len(inputs)} input(s) kept", ""]
    for owner in ("ours", "vendor", "none"):
        rows = sorted((k, v) for k, v in groups.items() if k[3] == owner)
        if not rows:
            continue
        lines += [f"### {titles[owner]}", "", "| kind | function | where | inputs | one of them |", "| --- | --- | --- | --- | --- |"]
        for (kind, function, where, _), names in rows:
            lines.append(f"| {kind} | `{function}` | `{where}` | {len(names)} | `{names[0]}` |")
        lines.append("")
    if not inputs:
        lines.append("Nothing was found.")
    summary = "\n".join(lines) + "\n"

    destination = os.environ.get("GITHUB_STEP_SUMMARY")
    if destination:
        with open(destination, "a", encoding="utf-8") as out:
            out.write(summary)
    print(summary)
    ours = sum(len(v) for k, v in groups.items() if k[3] == "ours")
    if ours:
        sys.exit(f"{ours} finding(s) in this repository's code")


if __name__ == "__main__":
    main()
