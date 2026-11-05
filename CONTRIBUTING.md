# Contributing

Issues and pull requests on GitHub are the whole process — there is no mailing list, no
CLA, and no meeting. This repository is an *implementation*; the format itself is decided
in [divejson/divejson](https://github.com/divejson/divejson), whose `GOVERNANCE.md` says
who decides and whose `CONTRIBUTING.md` says how a change to the format lands.

## What is here

| path | what |
| --- | --- |
| `include/`, `src/` | `libdivejson`: the core (`divejson.h`, `dive.c`, `json.c`, `identity.c`) and the libdivecomputer reader (`divejson_libdivecomputer.h`, `libdivecomputer.c`) |
| `cli/` | `divejson-raw` |
| `docs/libdivecomputer-mapping.md` | the reader's specification, which `src/libdivecomputer.c` implements; the two change together |
| `tests/` | the suite: `test_core.c`, `test_libdivecomputer.c`, `test_mapping.c`, `test_cli.py`, and in `fixtures/libdivecomputer/` the pairs |
| `tests/swiftpm/` | the executable the macOS job builds against libdc-swift |
| `fuzz/` | the libFuzzer target |
| `vendor/libdivecomputer` | the libdivecomputer fork, a submodule at `LIBDIVECOMPUTER_COMMIT` |
| `Package.swift` | the library for Apple hosts |
| `Dockerfile` | the published image |

## The checks

```bash
git submodule update --init
pip install -r tests/requirements.txt     # the divejson release the suite validates with
cmake -S . -B build -DDIVEJSON_LIBDIVECOMPUTER=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` runs four suites: the core's; the reader's through its one call; the mapping's
numbering rules over a scripted parser, `tests/test_mapping.c`, which supplies its own
`dc_*` symbols as a host does; and `tests/test_cli.py`, which runs `divejson-raw` as a caller would — with no environment —
compares its documents with the expected ones, exercises every refusal and its exit status,
and runs `divejson validate` over every document it and `test_mapping` produced. That last needs the `divejson`
release `tests/requirements.txt` pins on `PATH`; the test fails rather than skips without it.

CI runs more than that, and every job is required to pass:

| job | what it holds |
| --- | --- |
| the core | the core builds and passes with the reader off and no submodule checked out |
| the pins | the submodule, `LIBDIVECOMPUTER_COMMIT`, `NOTICE` and the libdc-swift release in `tests/swiftpm/Package.swift` name one commit |
| Linux | the build and the four suites |
| sanitizers | the same under AddressSanitizer and UndefinedBehaviorSanitizer, the vendored libdivecomputer instrumented too |
| fuzz | five minutes of `fuzz/fuzz_libdivecomputer.c`, which fuzzes the descriptor across the linked table as well as the bytes, since a caller may accept any descriptor it is handed; the fuzz build instruments the reader and the vendored parsers for coverage. `.github/scripts/fuzz_triage.py` re-runs every input it kept and fails the job on a finding in this repository's code; one inside `vendor/libdivecomputer` goes in the job summary and the uploaded findings, since the vendored copy is not patched here |
| macOS | an executable linking `Package.swift`'s library against libdc-swift's libdivecomputer reads both fixtures to the expected documents |
| Android | the library compiles under the NDK for `arm64-v8a` |
| the image | the `Dockerfile` builds, runs a fixture and carries its notices |

To run the sanitizers locally, configure with `CC=clang` and
`CFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"`; to fuzz, add
`-DDIVEJSON_FUZZ=ON` and run `build/fuzz_libdivecomputer`. The job in
`.github/workflows/ci.yml` shows how its corpus is seeded.

## The pairs

Each original under `tests/fixtures/libdivecomputer/` is paired with the document
`divejson-raw` must produce from it, so a mapping change shows up as a failing pair. **An
expected document is read by a human before it is committed** — the point of a pair is that
someone agreed with the answer. Regenerate the expected side, then read the diff:

```bash
python3 tests/test_cli.py build/divejson-raw --write
git diff tests/fixtures
```

`generator` and `exported_at` are the two members a document asserts about its own run, so
the comparison leaves both out (`tests/same_document.py`), as `divejson conform` does, and
`--write` stamps a fixed `exported_at`. A release never regenerates anything.

A new original lands with the descriptor of the computer that recorded it in
`tests/test_cli.py`'s `PAIRS` and a line in the fixtures' `README.md` saying where it came
from. An original is someone's dive: it is published only with their agreement.

## Changing the mapping

`docs/libdivecomputer-mapping.md` states every rule the reader follows, and a change to one
is a change to both, with the pairs regenerated and read. The rules
[`converting.md`](https://github.com/divejson/divejson/blob/main/docs/converting.md) states
for every reader are already decided here as there. **The identity namespace never changes**:
it would renumber every dive the reader has ever produced.

A member the format does not have yet is not written under `extensions`: the mapping lists it
as not mapped, the original buffer keeps it, and a release writes it once the format takes it
over. `extensions.divejson` holds only what `converting.md` puts there — provenance and the
inferred list.

## Moving libdivecomputer

The vendored commit moves together with the libdc-swift release an Apple host links, because
the host's libdivecomputer is what the library parses with there. Four places name it, and the
`pins` job fails unless they agree:

1. `vendor/libdivecomputer`, the submodule: `git -C vendor/libdivecomputer checkout <commit>`;
2. `LIBDIVECOMPUTER_COMMIT`, the commit on one line;
3. `NOTICE`, which names it for the binary's licence;
4. `tests/swiftpm/Package.swift`, the libdc-swift release whose own submodule is that commit.

`--version` changes with it, so every document written after the move says which parser
produced it. Regenerate the pairs and read the diff: a parser fix shows up there.

## Pull request titles

Use a semantic **PR title** — `<type>[(scope)][!]: <description>`, where type is one of
`feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `perf`, `ci`, `build`, `revert`.
Pull requests are squash-merged with the title as the commit subject, so it is the only
part of a branch that outlives the branch.

`.github/workflows/pr-title.yml` checks the format and re-runs when a title is edited —
a failing check is fixed by correcting the title, with nothing to push.

## Licensing of contributions

Everything in this repository outside `vendor/` is MIT. By contributing you license your
contribution under that license. `vendor/libdivecomputer` is libdivecomputer's own, under the
LGPL-2.1-or-later, and changes to it go to that project.
