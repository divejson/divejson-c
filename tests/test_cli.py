"""divejson-raw, run as a consumer runs it: arguments in, a document or an exit status out.

    python3 tests/test_cli.py path/to/divejson-raw

Every document the run produces is checked with `divejson validate`, from the divejson
release tests/requirements.txt pins, which has to be on PATH. `--write` regenerates the
expected documents instead of comparing against them (CONTRIBUTING.md says when).
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from same_document import compared

ROOT = Path(__file__).resolve().parent.parent
FIXTURES = ROOT / "tests" / "fixtures" / "libdivecomputer"
EXPORTED_AT = "2026-11-03T00:00:00+00:00"

# Each original, with the descriptor of the computer that recorded it.
PAIRS = [
    ("shearwater-perdix-3_D9772626_2026-10-26T1503", ["--vendor", "Shearwater", "--product", "Perdix 3", "--model", "14"]),
    ("suunto-ocean_253810000400_2026-10-26T1501", ["--vendor", "Suunto", "--product", "Ocean", "--model", "1"]),
]

# A Suunto Solution log, whose parser reports no start: tests/test_libdivecomputer.c's.
SOLUTION = bytes([0x00, 0x00, 0x00, 0x10, 0x10, 0xF0, 0xF0, 0x80, 0x02])
SOLUTION_ARGS = ["--vendor", "Suunto", "--product", "Solution", "--model", "0"]

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print(f"FAIL: {message}", file=sys.stderr)


def run(cli, args, stdin=None):
    # No environment at all: the program reads none, and a consumer may give it none.
    return subprocess.run([cli, *args], input=stdin, capture_output=True, env={}, timeout=120)


def main():
    args = sys.argv[1:]
    write = "--write" in args
    args = [arg for arg in args if arg != "--write"]
    if len(args) != 1:
        sys.exit(__doc__)
    cli = str(Path(args[0]).resolve())
    validator = shutil.which("divejson")
    if validator is None:
        sys.exit("divejson is not on PATH: pip install -r tests/requirements.txt")
    produced = Path(tempfile.mkdtemp(prefix="divejson-raw-"))

    version = run(cli, ["--version"]).stdout.decode().strip()
    commit = (ROOT / "LIBDIVECOMPUTER_COMMIT").read_text().strip()
    project = re.search(r"project\(divejson VERSION (\S+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    check(version == f"{project}+{commit[:7]}", f"--version printed {version!r}")
    check(len(version) <= 32, f"--version is {len(version)} characters, over 32")

    for name, descriptor in PAIRS:
        original = FIXTURES / f"{name}.bin"
        expected_path = FIXTURES / f"{name}.divejson"
        output = produced / f"{name}.divejson"
        result = run(cli, [*descriptor, "--exported-at", EXPORTED_AT, "--output", str(output), str(original)])
        check(result.returncode == 0, f"{name}: exit {result.returncode}: {result.stderr.decode()}")
        if result.returncode != 0:
            continue
        document = json.loads(output.read_text())
        check(document["generator"] == {"name": "divejson-raw", "version": version}, f"{name}: generator")
        if write:
            shutil.copyfile(output, expected_path)
            continue
        expected = json.loads(expected_path.read_text())
        check(compared(document) == compared(expected), f"{name}: differs from {expected_path.name}")
        # Standard input reads the same dive.
        piped = run(cli, [*descriptor, "--exported-at", EXPORTED_AT, "-"], stdin=original.read_bytes())
        check(piped.returncode == 0 and piped.stdout == output.read_bytes(), f"{name}: from standard input")

    # The fallback start, spelled as a consumer passes it.
    result = run(cli, [*SOLUTION_ARGS, "--fallback-start", "2026-10-02T08:36:00+00:00", "--exported-at", EXPORTED_AT, "-"], SOLUTION)
    check(result.returncode == 0, f"fallback: exit {result.returncode}: {result.stderr.decode()}")
    if result.returncode == 0:
        (produced / "fallback.divejson").write_bytes(result.stdout)
        dive = json.loads(result.stdout)["dives"][0]
        check(dive["started_at"] == "2026-10-02T08:36:00+00:00", f"fallback: started_at {dive['started_at']}")
    result = run(cli, [*SOLUTION_ARGS, "--fallback-start=2026-10-02T11:36:00", "--uuid", "2A7DCB1E-8B2B-4E6F-9C4E-0D6A5B0B8E11", "-"], SOLUTION)
    check(result.returncode == 0, f"fallback with a uuid: exit {result.returncode}")
    if result.returncode == 0:
        (produced / "fallback-uuid.divejson").write_bytes(result.stdout)
        document = json.loads(result.stdout)
        check(document["dives"][0]["uuid"] == "2a7dcb1e-8b2b-4e6f-9c4e-0d6a5b0b8e11", "the given uuid, lowercased")
        check(document["dives"][0]["started_at"] == "2026-10-02T11:36:00", "a wall-clock fallback")
        check(re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\+00:00", document["exported_at"]) is not None, "exported_at defaults to now, in UTC")

    # Refusals: a non-zero exit, a sentence on stderr, nothing on stdout, never a signal.
    refusals = [
        ("no start and no fallback", [*SOLUTION_ARGS, "-"], SOLUTION, 1, "no start"),
        ("an unknown computer", ["--vendor", "Suunto", "--product", "Solutions", "--model", "0", "-"], SOLUTION, 1, '"Suunto" "Solutions"'),
        ("an empty buffer", [*SOLUTION_ARGS, "-"], b"", 1, "empty"),
        ("zeros for a dive", [*PAIRS[0][1], "-"], b"\x00" * 4096, 1, "no start"),
        ("a missing file", [*SOLUTION_ARGS, str(produced / "missing.bin")], None, 1, "cannot open"),
        ("no model", ["--vendor", "Suunto", "--product", "Solution", "-"], SOLUTION, 2, "required"),
        ("a model that is no number", [*SOLUTION_ARGS[:-1], "one", "-"], SOLUTION, 2, "--model"),
        ("a malformed fallback", [*SOLUTION_ARGS, "--fallback-start", "2026-10-02 08:36", "-"], SOLUTION, 2, "--fallback-start"),
        ("an export time with no offset", [*SOLUTION_ARGS, "--exported-at", "2026-10-02T08:36:00", "-"], SOLUTION, 2, "--exported-at"),
        ("a malformed uuid", [*SOLUTION_ARGS, "--uuid", "x", "-"], SOLUTION, 2, "--uuid"),
        ("an unknown option", [*SOLUTION_ARGS, "--family", "1", "-"], SOLUTION, 2, "unknown option"),
        ("two files", [*SOLUTION_ARGS, "-", "-"], SOLUTION, 2, "more than one"),
        ("no file", SOLUTION_ARGS, None, 2, "FILE"),
    ]
    for label, refused_args, stdin, status, needle in refusals:
        result = run(cli, refused_args, stdin)
        check(result.returncode == status, f"{label}: exit {result.returncode}, expected {status}")
        check(needle in result.stderr.decode(), f"{label}: stderr {result.stderr.decode()!r} lacks {needle!r}")
        check(result.stdout == b"", f"{label}: wrote to stdout")

    documents = sorted(produced.glob("*.divejson"))
    check(len(documents) == len(PAIRS) + 2, f"{len(documents)} documents produced")
    validated = subprocess.run([validator, "validate", *map(str, documents)], capture_output=True, text=True)
    print(validated.stdout, end="")
    check(validated.returncode == 0, f"divejson validate: {validated.stdout}{validated.stderr}")

    shutil.rmtree(produced)
    if failures:
        sys.exit(f"{len(failures)} check(s) failed")
    print("cli: ok" + (" (expected documents written)" if write else ""))


if __name__ == "__main__":
    main()
