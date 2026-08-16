#!/usr/bin/env python3
"""Aggregate Cafe OS imports across a corpus of Wii U titles.

The point is to replace guesswork with measurement: which coreinit
functions do real titles actually call, and in what proportion.

Frequency is counted as "number of titles importing this symbol", not
total references. A function used by every title matters more than one
used a thousand times inside a single game.
"""
import argparse
import collections
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rpx


def find_rpx(root):
    if os.path.isfile(root):
        return [root]
    out = []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if f.lower().endswith((".rpx", ".rpl")):
                out.append(os.path.join(dirpath, f))
    return sorted(out)


def load_metadata(path):
    """Optional CSV: filename,title_id,name,publisher,year,genre,engine"""
    if not path or not os.path.exists(path):
        return {}
    meta = {}
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            key = (row.get("filename") or "").strip()
            if key:
                meta[key] = row
    return meta


def load_implemented(path):
    if not os.path.exists(path):
        return set()
    out = set()
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                out.add(line)
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))

    ap = argparse.ArgumentParser()
    ap.add_argument("corpus", help="directory of .rpx files (searched recursively)")
    ap.add_argument("--metadata", default=None,
                    help="optional CSV with per-title metadata")
    ap.add_argument("--implemented", default=os.path.join(here, "implemented.txt"))
    ap.add_argument("--out", default="census-out", help="output directory")
    ap.add_argument("--label", default="unnamed",
                    help="corpus label, e.g. 'homebrew' or 'commercial'")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    files = find_rpx(args.corpus)
    if not files:
        sys.exit(f"no .rpx/.rpl found under {args.corpus}")

    meta = load_metadata(args.metadata)
    implemented = load_implemented(args.implemented)

    # (library, symbol, kind) -> set of titles
    users = collections.defaultdict(set)
    per_title = []
    failures = []

    for path in files:
        base = os.path.basename(path)
        title = (meta.get(base, {}).get("name") or "").strip() \
                or os.path.splitext(base)[0]
        try:
            imports = rpx.load(path).imports()
        except Exception as exc:
            failures.append((base, str(exc)))
            continue

        count = 0
        for lib, entries in imports.items():
            for kind in ("functions", "data"):
                for sym in entries[kind]:
                    users[(lib, sym, kind)].add(title)
                    per_title.append((title, lib, sym, kind))
                    count += 1
        print(f"  {title:<40} {count:>5} imports")

    titles = sorted({t for t, _, _, _ in per_title})
    n = len(titles)
    if n == 0:
        sys.exit("no titles parsed successfully")

    os.makedirs(args.out, exist_ok=True)

    with open(os.path.join(args.out, "imports_by_title.csv"), "w",
              newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["title", "library", "symbol", "kind"])
        w.writerows(sorted(per_title))

    rows = []
    for (lib, sym, kind), who in users.items():
        rows.append((lib, sym, kind, len(who),
                     round(100.0 * len(who) / n, 1),
                     "yes" if sym in implemented else "no"))
    rows.sort(key=lambda r: (-r[3], r[0], r[1]))

    with open(os.path.join(args.out, "frequency.csv"), "w",
              newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["library", "symbol", "kind", "titles",
                    "pct_of_corpus", "implemented"])
        w.writerows(rows)

    # ---------------- summary ----------------

    print(f"\n{'=' * 62}")
    print(f"corpus '{args.label}': {n} titles, "
          f"{len(users)} distinct imports")
    if failures:
        print(f"{len(failures)} file(s) failed to parse:")
        for name, err in failures:
            print(f"  {name}: {err}")

    by_lib = collections.Counter(lib for lib, _, _ in users)
    print("\nlibraries by distinct imports:")
    for lib, c in by_lib.most_common():
        print(f"  {lib:<20} {c:>5}")

    # Coverage, weighted by how many titles need each symbol.
    core = [r for r in rows if r[0] == "coreinit"]
    if core:
        need = sum(r[3] for r in core)
        have = sum(r[3] for r in core if r[5] == "yes")
        print(f"\ncoreinit coverage: {have}/{need} title-requirements "
              f"({100.0 * have / need:.1f}%)")
        print(f"  distinct symbols: "
              f"{sum(1 for r in core if r[5] == 'yes')}/{len(core)}")

    print(f"\ntop {args.top} unimplemented coreinit imports:")
    print(f"  {'symbol':<40} {'titles':>7}  {'%':>6}")
    shown = 0
    for lib, sym, kind, cnt, pct, impl in rows:
        if lib != "coreinit" or impl == "yes":
            continue
        tag = "" if kind == "functions" else "  [data]"
        print(f"  {sym:<40} {cnt:>7}  {pct:>5}%{tag}")
        shown += 1
        if shown >= args.top:
            break

    print(f"\nwritten to {args.out}/")


if __name__ == "__main__":
    main()