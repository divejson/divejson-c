# divejson-c

[DiveJSON](https://divejson.org)'s C implementation: `libdivejson`, a library that writes
DiveJSON, and its readers. So far it has one, which reads a dive the way
[libdivecomputer](https://www.libdivecomputer.org) parses it — the de facto universal reader
of dive computers — so every computer libdivecomputer supports is a DiveJSON source through
one mapping. `divejson-raw` is that reader as a command.

The readers [divejson-py](https://github.com/divejson/divejson-py) has — UDDF, Subsurface,
FIT, the Suunto and Shearwater exports — come to C later, held to agreement with it by the
specification's shared fixture pairs. Until the first of them lands, this is not yet an
implementation in the sense the
[specification's README](https://github.com/divejson/divejson/blob/main/README.md) gives the
word: it reads none of the corpus's formats, so it runs none of its pairs, and its CI checks
its own output with `divejson validate` instead.

The format itself — the specification, the JSON Schema and the conformance corpus — lives in
[divejson/divejson](https://github.com/divejson/divejson).

## Read a libdivecomputer dive

```bash
divejson-raw --vendor Shearwater --product "Perdix 3" --model 14 dive.bin > dive.divejson
```

reads one dive's buffer, exactly as libdivecomputer's download handed it over, and writes a
DiveJSON document holding that dive. What the reader maps, and what it deliberately leaves
out, is [`docs/libdivecomputer-mapping.md`](docs/libdivecomputer-mapping.md).

The arguments are a contract, and change only with a release that says so:

| argument | |
| --- | --- |
| `FILE` | the dive's buffer; `-` reads it from standard input |
| `--vendor VENDOR`, `--product PRODUCT`, `--model MODEL` | required: the descriptor, as `dc_descriptor_get_vendor`, `_product` and `_model` answer them. Vendor and product are matched exactly against the descriptor table of the libdivecomputer built in, and the model, a decimal number, decides between entries sharing both. A family number is never taken: it is numbered per build. |
| `--fallback-start DATE-TIME` | the start to use where the parser reports none, as an RFC 3339 date-time with or without an offset, such as `2026-10-02T08:36:00+00:00`. A start the parser reports always wins. |
| `--uuid UUID` | the dive's uuid; without it, one is derived from the buffer's SHA-256 |
| `--exported-at DATE-TIME` | the document's `exported_at`, with its offset; without it, now, in UTC |
| `--output FILE` | where to write the document; without it, standard output |
| `--version` | prints the version and exits |

An option's value may also follow it after `=`. The program reads no environment variable
and no file but `FILE`, and writes nothing but the document.

**Run over bytes you did not choose, `divejson-raw` must be isolated**: in a process of its
own, with no secret in its environment, under a timeout and a memory ceiling, which it does
not set for itself. Its parsers are libdivecomputer's, and libdivecomputer's parsers trust
their input. Fuzzing the vendored copy finds reads past the end of the buffer in several
families' parsers and inputs on which the Suunto Nautic parser never returns; on such an
input the process can die on a signal or run until it is stopped. The vendored copy stays
exactly what its upstream released, the commit an Apple host links too, so those defects are
fixed upstream rather than here, and CI's fuzz run lists them on every pull request without
failing on them. A finding in this repository's own code fails it.

| exit status | meaning |
| --- | --- |
| 0 | the document was written |
| 1 | the dive was not read: no descriptor, or more than one, matches; the parser reports no start and none was given; the buffer is empty or unreadable; or the document could not be written. One line on standard error says which, and nothing is written to standard output. |
| 2 | the arguments are wrong: one missing, unknown or given twice, a model that is not a number, or a date-time or uuid not in its form |

`--version` prints `<version>+<commit>`, such as `0.1.0+1b201bb`: this library's version and
the first seven characters of the libdivecomputer commit built in, so it changes whenever
either does, and is never longer than 32 characters. The document's `generator.version` is the
same string.

### The image

Each release publishes `ghcr.io/divejson/divejson-c:<version>` for `linux/amd64` and
`linux/arm64`: a static `divejson-raw` at `/usr/local/bin/divejson-raw`, built against musl,
and its notices under `/usr/share/licenses/divejson-c/` — this repository's `LICENSE`,
`NOTICE`, and libdivecomputer's licence as `LGPL-2.1.txt`. Nothing else is in it, so another
image takes the binary with

```dockerfile
COPY --from=ghcr.io/divejson/divejson-c:0.1.0 /usr/local/bin/divejson-raw /usr/local/bin/divejson-raw
COPY --from=ghcr.io/divejson/divejson-c:0.1.0 /usr/share/licenses/divejson-c /usr/share/licenses/divejson-c
```

and `docker run -i ghcr.io/divejson/divejson-c:0.1.0 --vendor … -` runs it directly.

## Build it

CMake 3.16 or newer and a C11 compiler.

```bash
git clone --recurse-submodules https://github.com/divejson/divejson-c.git
cmake -S divejson-c -B build -DDIVEJSON_LIBDIVECOMPUTER=ON
cmake --build build
build/divejson-raw --version
```

| option | default | |
| --- | --- | --- |
| `DIVEJSON_LIBDIVECOMPUTER` | `OFF` | the libdivecomputer reader. Off, `libdivejson` is the core alone and builds with no libdivecomputer anywhere, and no submodule. |
| `DIVEJSON_CLI` | `ON` | `divejson-raw`, built when the reader is, with the vendored libdivecomputer compiled into it |
| `DIVEJSON_STATIC` | `OFF` | link `divejson-raw` statically, as the image does |
| `DIVEJSON_LIBDIVECOMPUTER_INCLUDE_DIR` | the vendored headers | the libdivecomputer headers the reader compiles against, for a host that brings its own |
| `DIVEJSON_TESTS` | `ON` | the test suite, which `ctest` runs |
| `DIVEJSON_FUZZ` | `OFF` | the libFuzzer target; clang only |

## Use the library

[`include/divejson.h`](include/divejson.h) is the core: a dive a reader produced, as JSON, the
members the reader inferred, and `divejson_document`, which wraps a dive in a document.
[`include/divejson_libdivecomputer.h`](include/divejson_libdivecomputer.h) is the reader's one
call, `divejson_libdivecomputer_read`, taking the buffer, the descriptor and the optional
fallback start and uuid. Neither header names a libdivecomputer type.

**The library links no libdivecomputer, even with the reader on.** It compiles against
libdivecomputer's headers and leaves every `dc_*` symbol for its host to supply, so an
application that already links libdivecomputer for its own downloads holds exactly one copy,
and the reader parses with the same code the download ran. That copy must be the commit this
repository vendors, which [`LIBDIVECOMPUTER_COMMIT`](LIBDIVECOMPUTER_COMMIT) names.

- **Apple platforms**: the root `Package.swift` is a Swift package whose `LibDiveJSON`
  product is the library with the reader on. It compiles none of libdivecomputer's sources;
  link [libdc-swift](https://github.com/deepsealabs/libdc-swift) beside it, at the release
  whose submodule is that commit (`1.20.0` today), and its libdivecomputer supplies the
  symbols. CI builds an executable that does exactly that and reads both fixtures through it.
- **Android**: build the library with the NDK's CMake toolchain and the reader on, and link
  the libdivecomputer the application builds for its downloads. CI compiles it for
  `arm64-v8a`.
- **Anything else**: `-DDIVEJSON_LIBDIVECOMPUTER=ON` gives `libdivejson.a`; link
  libdivecomputer after it.

The library hands its host **one dive**, and nothing of the host's: no `device` and no
`source_files` on the recording, which a host that has them adds. `divejson-raw` writes the
document-level members instead: the provenance, which names the descriptor, and the
generator.

## libdivecomputer

What is vendored is the [deepsealabs/libdivecomputer](https://github.com/deepsealabs/libdivecomputer)
fork, which adds the Suunto Nautic family (the Suunto Ocean among it) to upstream, at the
commit [`LIBDIVECOMPUTER_COMMIT`](LIBDIVECOMPUTER_COMMIT) names: `vendor/libdivecomputer` is a
git submodule at that commit. It is compiled as libdc-swift compiles it — every source but the
Windows serial one, `HAVE_PTHREAD_H` and `ENABLE_LOGGING` defined, no `config.h` — plus
`HAVE_LINUX_SERIAL_H` on Linux, where glibc's `TIOCGSERIAL` needs `<linux/serial.h>`. The
reader never reaches a transport; the serial code is compiled only because the rest of
libdivecomputer refers to it.

That commit moves only together with the libdc-swift release an Apple host links, and each
release's notes name it. [`CONTRIBUTING.md`](CONTRIBUTING.md) says how the pin moves.

## Releasing

A `v<version>` tag on `main` is the whole of a release:
[`.github/workflows/release.yml`](.github/workflows/release.yml) checks that the tag, the
`project()` version in `CMakeLists.txt`, `DIVEJSON_VERSION` in `include/divejson.h` and a
`## <version>` section of [`CHANGELOG.md`](CHANGELOG.md) all agree, refuses otherwise, then
publishes the image for both architectures and a GitHub release whose notes are that section
and the libdivecomputer commit. So a release is a pull request moving those three to the new
version, merged, and then the tag pushed on its commit.

## License

MIT — see [`LICENSE`](LICENSE). The `divejson-raw` binary, and the image that carries it,
link libdivecomputer, which is LGPL-2.1-or-later; [`NOTICE`](NOTICE) names it, its fork and
its commit, and the image carries the licence text beside it. Everything needed to rebuild
`divejson-raw` against a modified libdivecomputer is here: change `vendor/libdivecomputer` and
build as above. The library itself contains no libdivecomputer code, and the core builds
without it.
