#!/usr/bin/env python3
"""Check that the bundle examples embedded in the documentation actually parse.

A documentation example that does not parse is worse than no example: it is the first thing
a new author copies. The fixtures in tests/data/ prove the *reader* works; this proves that
the *examples we publish* are among the inputs the reader accepts.

Usage: check_docs.py <linguamap_check binary> <file.md>...
"""

import pathlib
import re
import subprocess
import sys
import tempfile


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("usage: check_docs.py <checker> <file.md>...", file=sys.stderr)
        return 2

    checker = argv[1]
    failures = 0
    checked = 0

    for name in argv[2:]:
        path = pathlib.Path(name)
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as error:
            print(f"FAIL {path}: {error}", file=sys.stderr)
            failures += 1
            continue

        # A bundle example is a fenced block that starts with the format's magic line.
        for match in re.finditer(r"```[^\n]*\n(#!linguamap[^\n]*\n.*?)```", text, re.S):
            bundle = match.group(1)
            checked += 1

            # Report the line the block starts on so a failure points into the document.
            line = text[: match.start()].count("\n") + 1
            with tempfile.NamedTemporaryFile(
                "w", suffix=".lmz.txt", delete=False, encoding="utf-8"
            ) as tmp:
                tmp.write(bundle)
                tmp_path = tmp.name

            try:
                result = subprocess.run([checker, tmp_path], capture_output=True, text=True)
            finally:
                pathlib.Path(tmp_path).unlink(missing_ok=True)

            if result.returncode != 0:
                failures += 1
                print(f"FAIL {path}:{line}: the example in this document does not parse")
                for message in (result.stdout + result.stderr).strip().splitlines()[:3]:
                    print(f"     {message}")

    if failures:
        print(f"{failures} of {checked} documentation example(s) failed", file=sys.stderr)
        return 1

    print(f"{checked} documentation example(s) parse")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
