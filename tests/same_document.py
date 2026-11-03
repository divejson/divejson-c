"""Whether two DiveJSON documents say the same: python3 tests/same_document.py EXPECTED PRODUCED.

The generator and the export time move without the mapping moving, so they are left out of
the comparison, as `divejson conform` leaves them out of a pair's.
"""

import json
import sys


def compared(document):
    return {key: value for key, value in document.items() if key not in ("generator", "exported_at")}


def same(expected, produced):
    with open(expected, encoding="utf-8") as left, open(produced, encoding="utf-8") as right:
        return compared(json.load(left)) == compared(json.load(right))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    if not same(sys.argv[1], sys.argv[2]):
        sys.exit(f"{sys.argv[2]} differs from {sys.argv[1]}")
    print(f"{sys.argv[2]}: the same as {sys.argv[1]}")
