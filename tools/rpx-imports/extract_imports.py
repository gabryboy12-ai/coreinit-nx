#!/usr/bin/env python3
"""List the named imports of a Wii U RPX file."""
import json
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpx


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    as_json = "--json" in sys.argv

    if len(args) != 1:
        sys.exit("usage: extract_imports.py [--json] <file.rpx>")

    imports = rpx.load(args[0]).imports()

    if as_json:
        print(json.dumps(imports, indent=2, sort_keys=True))
        return

    if not imports:
        print("no imports found")
        return

    total = 0
    for lib in sorted(imports):
        entries = imports[lib]
        n = len(entries["functions"]) + len(entries["data"])
        total += n
        print(f"\n=== {lib} ({n}) ===")
        for kind in ("functions", "data"):
            for sym in entries[kind]:
                tag = "" if kind == "functions" else "  [data]"
                print(f"  {sym}{tag}")

    print(f"\n{total} imports across {len(imports)} libraries")


if __name__ == "__main__":
    main()