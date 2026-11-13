#!/usr/bin/env python3
"""Verify that the palette vendored into the plugin is exactly the published one.

`tests/data/colorization.json` is a verbatim copy of
`translate-parallel-3-side:engine/colorization.json`, which is the single source of truth
for word-map colours. `src/colorization.h` is what the plugin actually renders. If those
two ever disagree, the plugin silently paints a meaning the wrong colour, which is worse
than not painting it at all. This check makes that a build failure instead.

Usage: check_palette.py <colorization.json> <colorization.h>
"""

import json
import re
import sys


def read_reference(path):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)

    if data.get("schema") != "linguamap-colorization":
        raise SystemExit(f"{path}: unexpected schema {data.get('schema')!r}")

    codes = [entry["code"] for entry in data["codes"]]
    colours = data["colors"]
    if sorted(codes) != sorted(colours):
        raise SystemExit(f"{path}: 'codes' and 'colors' describe different code sets")

    return codes, {code: (colours[code]["light"], colours[code]["dark"]) for code in codes}


def read_header(path):
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    # Rows look like:  { "n", "noun", 0x1a5fc4, 0x7fb2f0 },
    row = re.compile(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*0x([0-9a-fA-F]{6})\s*,\s*0x([0-9a-fA-F]{6})\s*\}')
    rows = row.findall(text)
    if not rows:
        raise SystemExit(f"{path}: no palette rows found (has the table been reformatted?)")

    codes = [r[0] for r in rows]
    table = {r[0]: ("#" + r[2].lower(), "#" + r[3].lower()) for r in rows}
    labels = {r[0]: r[1] for r in rows}
    return codes, table, labels


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)

    ref_codes, ref_table = read_reference(argv[1])
    hdr_codes, hdr_table, _labels = read_header(argv[2])

    failures = []

    # Order matters: the C++ enum is declared in this order and the table is indexed by it.
    if ref_codes != hdr_codes:
        failures.append(
            "code order differs\n"
            f"  reference: {ref_codes}\n"
            f"  header:    {hdr_codes}\n"
            "  (the C++ enum and the table must list the codes in the reference order)"
        )

    for code in ref_codes:
        if code not in hdr_table:
            failures.append(f"code {code!r} is missing from the header")
            continue
        if ref_table[code] != hdr_table[code]:
            failures.append(f"code {code!r}: reference {ref_table[code]} but header {hdr_table[code]}")

    if failures:
        print("palette mismatch:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(f"palette ok ({len(ref_codes)} codes, light and dark match the reference)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
