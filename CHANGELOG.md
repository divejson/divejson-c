# Changelog

## 0.1.0

- `libdivejson`'s core: a dive a reader produced, as JSON, and `divejson_document`, which
  wraps one in a DiveJSON 1.0 document.
- The libdivecomputer reader, `divejson_libdivecomputer_read`: one dive's buffer and its
  descriptor into a DiveJSON dive, mapped as `docs/libdivecomputer-mapping.md` says. The
  library compiles against libdivecomputer's headers and links none of it.
- `divejson-raw`, the reader as a command, with the deepsealabs libdivecomputer fork built
  in, published as `ghcr.io/divejson/divejson-c` for linux/amd64 and linux/arm64.
