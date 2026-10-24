#!/usr/bin/env python3
"""Regression tests for dxsim.

  python3 tests/run_tests.py [--update] [path/to/dxsim]

1. `dxsim -check` parses every block of the built-in cycle text.
2. Every tests/*.txt program runs; its trace must equal tests/expected/<name>.out
   (--update rewrites the expected files after a reviewed change).
3. Hand-checked results from the programmer's manual (hole counts and positions,
   circle radii, macro parameters...), independent of the golden files.
4. Coverage: the tests together must execute most of the built-in cycle text.
"""
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FAIL = []


def run(dxsim, *args):
    p = subprocess.run([dxsim] + list(args), cwd=HERE, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)
    return p.returncode, p.stdout


def check(cond, what):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        FAIL.append(what)


MOVE = re.compile(r"^(\S+)\s+(RAPID|FEED|ARC2|ARC3|SPIRL)\s+X(-?[\d.]+) Y(-?[\d.]+) Z(-?[\d.]+)(.*)$")


def moves(trace):
    out = []
    for line in trace.splitlines():
        m = MOVE.match(line)
        if m:
            out.append((m.group(1), m.group(2), float(m.group(3)), float(m.group(4)),
                        float(m.group(5)), m.group(6)))
    return out


def holes(trace):
    """Drill plunges: FEED moves that change only Z downwards, end points."""
    res, last, same = [], None, False     # same: no XY motion since the last plunge
    for where, kind, x, y, z, rest in moves(trace):
        if last and (abs(x - last[0]) > 1e-6 or abs(y - last[1]) > 1e-6):
            same = False
        if kind == "FEED" and last and not (abs(x - last[0]) > 1e-6 or abs(y - last[1]) > 1e-6) \
                and z < last[2] - 1e-6:
            if same:                    # another peck of the same hole
                res[-1] = (x, y, min(z, res[-1][2]))
            else:
                res.append((x, y, z))
                same = True
        last = (x, y, z)
    return res


def near(a, b, tol=2e-4):
    return abs(a - b) <= tol


def manual_checks(dxsim):
    # five concentric circles R5..R1 (manual :1341)
    rc, t = run(dxsim, "man_1341.txt")
    arcs = [m for m in moves(t) if m[1] == "ARC2"]
    radii = sorted(float(re.search(r"R([\d.]+)", m[5]).group(1)) for m in arcs)
    check(rc == 0 and radii == [1.0, 2.0, 3.0, 4.0, 5.0] and
          all("-360.000 deg" in m[5] for m in arcs), "manual :1341 five full circles R1..R5")

    # G81 example of chapter 5: four holes, bottom at Z-1.05
    rc, t = run(dxsim, "man_g81.txt")
    h = holes(t)
    want = [(1.0, 0.5, -1.05), (0.5, 1.0, -1.05), (2.0, 1.0, -1.05), (2.5, 0.5, -1.05)]
    check(rc == 0 and len(h) == 4 and all(near(a[i], b[i]) for a, b in zip(h, want) for i in range(3)),
          "manual G81 example: 4 holes at the programmed points, Z-1.05")

    # G191 frame (fig 5-11): "Drill (18) Holes"
    rc, t = run(dxsim, "cyc_g191.txt")
    h = holes(t)
    xs = sorted(set(round(p[0], 4) for p in h))
    ys = sorted(set(round(p[1], 4) for p in h))
    check(len(h) == 18 and xs[0] == 2.5 and xs[-1] == 5.5 and ys[0] == 2.0 and ys[-1] == 3.5 and
          all(near(p[2], -0.5) for p in h), "fig 5-11 G191 frame: 18 holes .5 deep on the 3 x 1.5 frame")

    # G181 row (fig 5-9): 4 holes from (2.5,1.5) to (5.5,3)
    rc, t = run(dxsim, "cyc_g181.txt")
    h = holes(t)
    check(len(h) == 4 and near(h[0][0], 2.5) and near(h[0][1], 1.5) and near(h[-1][0], 5.5) and
          near(h[-1][1], 3.0) and all(near(p[2], -0.5) for p in h),
          "fig 5-9 G181 row: 4 holes .5 deep from (2.5,1.5) to (5.5,3)")

    # bolt circles (fig 5-8 and 5-10): 6 holes at 30..330 degrees on R1.75
    for name, cx, cy in (("cyc_bolt.txt", 2.0, 2.0), ("cyc_g181bc.txt", 2.0, 3.0)):
        rc, t = run(dxsim, name)
        h = holes(t)
        ang = sorted(math.degrees(math.atan2(p[1] - cy, p[0] - cx)) % 360 for p in h)
        rad = [math.hypot(p[0] - cx, p[1] - cy) for p in h]
        check(len(h) == 6 and all(near(a, b, 0.01) for a, b in zip(ang, range(30, 360, 60))) and
              all(near(r, 1.75) for r in rad), "%s: 6 holes at 30..330 deg on R1.75" % name)

    # nested loops (fig 10-2): 6 rows of 7 holes
    rc, t = run(dxsim, "man_loop.txt")
    h = holes(t)
    check(len(h) == 42 and len(set(round(p[1], 4) for p in h)) == 6,
          "fig 10-2 nested =N loops: 42 holes in 6 rows")

    # G83: total .9 from Z.1, pecks .3 then .2
    rc, t = run(dxsim, "cyc_g83.txt")
    feeds = [m[4] for m in moves(t) if m[1] == "FEED"]
    check([round(z, 4) for z in feeds] == [-0.2, -0.4, -0.6, -0.8], "G83 peck depths -.2 -.4 -.6 -.8")

    # macro #3 with '*' parameters (chapter 10)
    rc, t = run(dxsim, "man_macro.txt")
    zs = [round(m[4], 4) for m in moves(t)][2:7]
    check(zs == [-0.5, 0.5, -0.5, -0.2, 0.7], "macro =#3 Z*... takes its 7 parameters in order")

    # mirror image (fig 4-10): four copies around X2 Y1
    rc, t = run(dxsim, "man_mirror.txt")
    cut = [m for m in moves(t) if m[1] == "FEED"]
    xs = [m[2] for m in cut]
    ys = [m[3] for m in cut]
    check(len(cut) == 24 and near(min(xs), 0.7) and near(max(xs), 3.3) and near(min(ys), 0.25) and
          near(max(ys), 1.75), "fig 4-10 mirror: shape copied into four quadrants about X2 Y1")

    # arcs (fig 3-3): centre, radius and incremental forms give the same path
    rc, t = run(dxsim, "man_arcs.txt")
    arcs = [m[1:5] + (re.sub(r"\s+F.*", "", m[5]),) for m in moves(t) if m[1].startswith("ARC")]
    check(len(arcs) == 6 and arcs[0:2] == arcs[2:4] == arcs[4:6],
          "fig 3-3: I/J, R and G91 arc forms agree")

    # expression evaluator
    for expr, want in (("1+2*3", 7.0), ("(1+2)*3", 9.0), ("-2*3", -6.0), ("SIN(30)", 0.5),
                       ("ATN(1)", 45.0), ("SQR(16)+ABS(-2)", 6.0), ("DTF(3,4)", 5.0),
                       ("#PI", math.pi), ("10/4", 2.5), ("2-3-4", -5.0), ("ABS(-3)-1", 2.0)):
        rc, out = run(dxsim, "-e", expr)
        check(rc == 0 and near(float(out), want, 1e-7), "expression %s = %g" % (expr, want))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    update = "--update" in sys.argv
    dxsim = os.path.abspath(args[0]) if args else os.path.join(ROOT, "dxsim")
    if not os.path.exists(dxsim):
        sys.exit("build dxsim first (make) or pass its path")

    rc, out = run(dxsim, "-check")
    check(rc == 0 and ", 0 failed" in out, "-check: " + out.strip().splitlines()[-1])

    covered = set()
    progs = sorted(f for f in os.listdir(HERE) if f.endswith(".txt"))
    os.makedirs(os.path.join(HERE, "expected"), exist_ok=True)
    for f in progs:
        rc, out = run(dxsim, f)
        exp = os.path.join(HERE, "expected", f[:-4] + ".out")
        if update or not os.path.exists(exp):
            open(exp, "w").write(out)
        check(rc == 0 and open(exp).read() == out, "trace of %s matches expected/%s" %
              (f, os.path.basename(exp)))
        rc, cov = run(dxsim, f, "-q", "-cov")
        covered |= set(l.split()[1] for l in cov.splitlines() if l.startswith("COV "))

    manual_checks(dxsim)

    corpus = os.path.join(ROOT, "corpus_689.txt")
    blocks = [l.split("\t") for l in open(corpus) if not l.startswith("#")]
    nonempty = [b[0] for b in blocks if b[2].strip()]
    n = sum(1 for b in nonempty if b in covered)
    check(n >= 600, "built-in cycle coverage: %d of %d non-empty blocks executed" % (n, len(nonempty)))
    missing = [b for b in nonempty if b not in covered]
    if missing:
        print("      not executed: " + " ".join(missing))

    print("%d failures" % len(FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
