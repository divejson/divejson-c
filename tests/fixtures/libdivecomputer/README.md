# libdivecomputer pairs

Each `.bin` is one dive's buffer, exactly as libdivecomputer's download handed it over, and
the `.divejson` beside it is the document `divejson-raw` must produce from it.
`tests/test_cli.py`'s `PAIRS` holds the descriptor each was read with.

Both originals are one decompression dive on 2026-10-26 at Dahab, Egypt, recorded on two
computers the diver wore together and published with the diver's agreement:

| original | computer | descriptor |
| --- | --- | --- |
| `shearwater-perdix-3_D9772626_2026-10-26T1503.bin` | Shearwater Perdix 3, serial D9772626 | `Shearwater` / `Perdix 3` / 14 |
| `suunto-ocean_253810000400_2026-10-26T1501.bin` | Suunto Ocean, serial 253810000400 | `Suunto` / `Ocean` / 1 |

The Ocean's buffer is the Suunto Nautic parser's input: the decompressed `/Data` stream with
`/Summary` appended. Its log holds a GPS fix, so the parser states its start, and neither
original reads through a fallback start; the suite drives that branch with a synthetic
Suunto Solution log, whose parser reports no start at all.

The same dive is in the specification's corpus from the computers' own exports — the Perdix 3
as `shearwater_db/perdix3-deco-dive-11` and `shearwater_xml/perdix3-deco-dive-11`, the Ocean
as `suunto_json/ocean-heart-rate` and `fit/ocean-heart-rate` — which is what the expected
documents here were read against.
