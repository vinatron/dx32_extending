#!/usr/bin/env python3
"""Extract the built-in cycle macro text from a BMDC firmware image.

Firmware 6.89 (repo-root BMDC.BIN) keeps its canned-cycle programs as
NUL-terminated macro-language blocks between 0x26852 and 0x27b2e.  The region
starts with two tables of 32-bit pointers into the text:

  table A  0x26852  41 entries  `%n` calls (FUN_00020422 -> state 5, 0x1ca78)
  table B  0x268f6  12 entries  `%n` jumps (state 3, 0x1c9f0)

Usage: extract_corpus.py [BMDC.BIN] [-o corpus.txt]

The output lists every block with its file offset, and marks each block that
a table entry points at (or into: some entries point past the first byte of
a block, which the interpreter then runs from that byte).
"""
import argparse
import os
import struct

TEXT_LO, TEXT_HI = 0x26852, 0x27b2e
TABLES = (("A", 0x26852, 41), ("B", 0x268f6, 12))


def read_tables(img):
    tabs = {}
    for name, base, count in TABLES:
        tabs[name] = [struct.unpack(">I", img[base + 4 * i:base + 4 * i + 4])[0]
                      for i in range(count)]
    return tabs


def blocks(img):
    """Yield (offset, text) for every NUL-terminated block of the text area."""
    start = TABLES[-1][1] + 4 * TABLES[-1][2]
    pos = start
    while pos < TEXT_HI:
        end = img.index(b"\0", pos)
        yield pos, img[pos:end].decode("latin-1")
        pos = end + 1


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", default=os.path.join(here, "..", "..", "BMDC.BIN"))
    ap.add_argument("-o", "--output")
    args = ap.parse_args()
    img = open(args.image, "rb").read()
    tabs = read_tables(img)
    entries = {}
    for name, ptrs in tabs.items():
        for i, p in enumerate(ptrs):
            if p:
                entries.setdefault(p, []).append("%s%d" % (name, i + 1))
    out = ["# BMDC built-in cycle macro text, extracted by tools/extract_corpus.py",
           "# format: offset<TAB>entries<TAB>block   (entries: table+index pointing here;",
           "#         '+k' means the entry points k bytes into the block)"]
    for name, ptrs in tabs.items():
        out.append("# table %s: %s" % (name, " ".join("%d=%05x" % (i + 1, p) for i, p in enumerate(ptrs))))
    for off, text in blocks(img):
        marks = []
        for k in range(len(text) + 1):
            for e in entries.get(off + k, []):
                marks.append(e if k == 0 else "%s+%d" % (e, k))
        out.append("%05x\t%s\t%s" % (off, ",".join(marks), text))
    data = "\n".join(out) + "\n"
    if args.output:
        open(args.output, "w").write(data)
    else:
        print(data, end="")


if __name__ == "__main__":
    main()
