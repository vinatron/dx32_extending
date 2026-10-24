#!/usr/bin/env python3
"""Check the grammar of LANGUAGE.md against the extracted corpus (independent of dxsim).

This is a recognizer, not the simulator: it splits every block into the token
classes of the draft grammar and reports blocks it cannot place, plus blocks
that use constructs whose firmware meaning is still an open question.

Usage: check_grammar.py [corpus_689.txt] [-v]
"""
import os
import re
import sys

OPS = ("EQ", "NE", "LT", "LE", "GT", "GE")                      # table at 0x20256
FUNCS1 = ("ABS SIN COS SQR TAN ATN LOG ALG ASN ACS LGE E2X HSN HCS HTN HAT "
          "NOT FFB RDI RDP GET").split()                         # table at 0x26302, codes 0x10-0x60
FUNCS2 = "DTF WRI WRP PUT XOR AND LOR LSR LSL ROR ROL SET CLR TST MRG".split()  # codes >= 0x64

NUM = r"[+-]?(?:\d+\.?\d*|\.\d+)"
VAR = r"[A-Z](?:[1-9]|[1-4]\d|50)"          # letter + index; see 0x23eb2 / 0x251ca
ASSIGN_HEAD = re.compile(r"^[A-Z]\d{1,2}=")  # block-start test at 0x1ed88


class ParseError(Exception):
    pass


class Expr:
    """Recursive descent over the evaluator at 0x24fc8 (expr/term/factor)."""

    def __init__(self, s):
        self.s, self.i = s.upper().replace(" ", ""), 0

    def peek(self):
        return self.s[self.i] if self.i < len(self.s) else ""

    def parse(self):
        self.expr()
        if self.i != len(self.s):
            raise ParseError("junk after expression: %r" % self.s[self.i:])

    def expr(self):
        self.term()
        while self.peek() in ("+", "-") and self.peek():
            self.i += 1
            self.term()

    def term(self):
        self.factor()
        while self.peek() in ("*", "/") and self.peek():
            self.i += 1
            self.factor()

    def factor(self):
        s, c = self.s, self.peek()
        if c in ("+", "-"):
            self.i += 1
            return self.factor()
        if c == "(":
            self.i += 1
            self.expr()
            self.expect(")")
            return
        if s.startswith("#PI", self.i):
            self.i += 3
            return
        m = re.compile(r"\d+\.?\d*|\.\d+").match(s, self.i)
        if m:
            self.i = m.end()
            return
        name = s[self.i:self.i + 3]
        if name in FUNCS1 or name in FUNCS2:
            self.i += 3
            self.expect("(")
            self.expr()
            if name in FUNCS2:
                self.expect(",")
                self.expr()
            self.expect(")")
            return
        m = re.compile(VAR).match(s, self.i)
        if m:
            self.i = m.end()
            return
        raise ParseError("bad factor at %r" % s[self.i:])

    def expect(self, ch):
        if self.peek() != ch:
            raise ParseError("expected %r at %r" % (ch, self.s[self.i:]))
        self.i += 1


ITEM = re.compile("|".join([
    r"(?P<cond>\?(?P<lhs>%s)<(?P<op>[A-Z]{2})>(?P<rhs>%s|%s))" % (VAR, NUM, VAR),
    r"(?P<endif>!)",
    r"(?P<call>=(?:%(?:\d+|@)|#\d+|L\d+))",     # '=' sets 0x32e3 bit 0: call / goto
    r"(?P<jump>%(?:\d+|@))",                      # table B jump (0x203fe, state 3)
    r"(?P<ret>[&$|])",
    r"(?P<label>L\d+)",
    r"(?P<subdef>#\d+)",
    r"(?P<seq>:\d+)",
    r"(?P<word>[A-KM-Z](?:\*(?:%s)?|%s))" % (NUM, NUM),
    r"(?P<stray>=%s)" % NUM,                     # see LANGUAGE.md open question 1
    r"(?P<comment>;.*$|\"[^\"]*\"|'[^']*'|`[^`]*`)",
]))


def tokenize(block):
    """Return a list of (kind, text) for one NUL-terminated block."""
    up = block.upper()
    toks = []
    if ASSIGN_HEAD.match(up):
        end = len(up)
        for t in "!;\r":
            if t in up:
                end = min(end, up.index(t))
        target, expr = up[:end].split("=", 1)
        Expr(expr).parse()
        toks.append(("assign", up[:end]))
        up = up[end:]
    i = 0
    while i < len(up):
        if up[i] in " \t":
            i += 1
            continue
        m = ITEM.match(up, i)
        if not m:
            raise ParseError("cannot place %r" % up[i:])
        kind = m.lastgroup if m.lastgroup not in ("lhs", "op", "rhs") else "cond"
        if kind == "cond" and m.group("op") not in OPS:
            raise ParseError("unknown comparison %r" % m.group("op"))
        toks.append((kind, m.group(0)))
        i = m.end()
    return toks


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    verbose = "-v" in sys.argv
    path = args[0] if args else os.path.join(here, "..", "corpus_689.txt")
    counts, bad, odd = {}, [], []
    nblocks = 0
    for line in open(path):
        if line.startswith("#"):
            continue
        off, _, text = line.rstrip("\n").split("\t", 2)
        if not text:
            continue
        nblocks += 1
        try:
            toks = tokenize(text)
        except ParseError as e:
            bad.append((off, text, str(e)))
            continue
        for k, _ in toks:
            counts[k] = counts.get(k, 0) + 1
        if any(k == "stray" for k, _ in toks):
            odd.append((off, text))
        if verbose:
            print(off, text, toks)
    print("blocks: %d  unparsed: %d" % (nblocks, len(bad)))
    print("tokens: " + ", ".join("%s=%d" % kv for kv in sorted(counts.items())))
    for off, text in odd:
        print("open question (mid-block '='): %s %s" % (off, text))
    for off, text, err in bad:
        print("UNPARSED %s %-30s %s" % (off, text, err))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
