#!/usr/bin/env python3
"""
Patch BXX.EXE so "9 EXIT TO DOS" really exits to DOS instead of re-running the last
application launched from BXX (NC-VIEW -> NC, WINDOWS -> WIN, ...).

Why it happens (details in BXX_EXIT_TO_DOS.md):
  BXX launches the helper applications by writing a one-line batch file and quitting:
      sprintf(buf, "%s:\\NEXT.BAT", drive);  f = fopen(buf, "wb");
      fputs("call EZ-VIEW", f);  fclose(f);  quit_flag = 1;
  The batch file that started BXX then runs NEXT.BAT.  "EXIT TO DOS" sets the same quit_flag
  but never touches NEXT.BAT, so the outer batch file runs whatever the last launcher left there.

What the patch does:
  Exit-to-DOS <Y> goes through the NC-VIEW launcher code with a "\\n" string instead of
  "call EZ-VIEW", so it overwrites NEXT.BAT with an empty line before quitting (the same thing
  4.79's own "BMDC communication failure" path writes).  The launcher's string becomes a
  register (si, unused in this function) that two small stubs load:
      handler:  mov ax,[bp+key]; cmp ax,'y'; je +; cmp ax,'Y'; jne <not Y>
             +: mov si,"\\n";        jmp launcher       ; EXIT TO DOS
                mov si,"call EZ-VIEW"; jmp launcher     ; NC-VIEW (dispatch retargeted here)
                nop nop nop
      launcher: ... mov ax,si ; nop  (was mov ax,"call EZ-VIEW") ...
  The stubs fit in the 29 bytes of the original <Y>/<y> test, which was written with
  cmp/je/jmp triples.  No far call or relocated word is moved; NC-VIEW behaves as before.

Known builds (both have the same bug):
  4.79  "BOSS BPC-MC V4.79.22", 577322 bytes, md5 99a320a1...   (repo root BXX.EXE)
  4.23  "BOSS DX/32 V4.23",     403490 bytes, md5 657801ef...   (my_tc22_files/BXX.EXE)

Usage:
  python3 tools/patch_bxx_exit_dos.py BXX.EXE BXX_PATCHED.EXE
  python3 tools/patch_bxx_exit_dos.py --check BXX_xx.EXE
"""
import argparse
import hashlib
import struct
import sys

# All offsets are FILE offsets (MZ header included).
BUILDS = {
    "4.79": dict(
        size=577322,
        ds=0x54F50,          # file offset of DGROUP (segment 6135 in Ghidra)
        key=-0x272,          # [bp-0x272] = key code returned by the prompt
        handler=0x76CB,      # cmp word [bp-0x272],'y' ... jmp quit   (29 bytes)
        not_y=0x76E8,        # code for the other keys (F5/F6 ...)
        launcher=0x6C3E,     # NC-VIEW launcher: writes "call EZ-VIEW" to NEXT.BAT
        str_mov=0x6C7B,      # mov ax,<"call EZ-VIEW">
        dispatch=0x916E,     # jmp launcher   (menu key dispatch)
        ezview=0x0831,       # DS:"call EZ-VIEW"
        newline=0x14A8,      # DS:"\n" (used by the comm-failure NEXT.BAT clear)
        quit_flag=-0x2CA,
    ),
    "4.23": dict(
        size=403490,
        ds=0x585E0,
        key=-0xCE,
        handler=0x9B1B,
        not_y=0x9B38,
        launcher=0x8C2E,
        str_mov=0x8C6B,
        dispatch=0xAB40,
        ezview=0x06C1,
        newline=0x1D8A,      # tail "\n" of an unrelated "... %9s;\n" table string
        quit_flag=-0xBC,
    ),
}

QUIT_JMP = {"4.79": 0x91B4, "4.23": 0xAB86}


def w16(v):
    return struct.pack("<h" if v < 0 else "<H", v)


def rel16(src_next, dst):
    return struct.pack("<h", dst - src_next)


def original_bytes(b, ver):
    """Bytes the unpatched build must have at each patch site."""
    h = b["handler"]
    handler = (
        b"\x83\xbe" + w16(b["key"]) + b"\x79"          # cmp word [bp+key],0x79
        + b"\x75\x03"                                  # jne +3
        + b"\xe9" + rel16(h + 10, h + 0x14)            # jmp Y
        + b"\x83\xbe" + w16(b["key"]) + b"\x59"        # cmp word [bp+key],0x59
        + b"\x74\x03"                                  # je Y
        + b"\xe9" + rel16(h + 0x14, b["not_y"])        # jmp not_y
        + b"\xc7\x86" + w16(b["quit_flag"]) + b"\x01\x00"   # Y: mov word [bp+flag],1
        + b"\xe9" + rel16(h + 0x1D, QUIT_JMP[ver])     # jmp quit
    )
    return {
        b["handler"]: handler,
        b["str_mov"]: b"\xb8" + w16(b["ezview"]),
        b["dispatch"]: b"\xe9" + rel16(b["dispatch"] + 3, b["launcher"]),
    }


def patched_bytes(b):
    h, L = b["handler"], b["launcher"]
    handler = (
        b"\x8b\x86" + w16(b["key"])                    # mov ax,[bp+key]
        + b"\x3d\x79\x00"                              # cmp ax,'y'
        + b"\x74\x05"                                  # je  h+0x0e
        + b"\x3d\x59\x00"                              # cmp ax,'Y'
        + b"\x75" + bytes([b["not_y"] - (h + 0x0E)])   # jne not_y
        + b"\xbe" + w16(b["newline"])                  # mov si,"\n"
        + b"\xe9" + rel16(h + 0x14, L)                 # jmp launcher
        + b"\xbe" + w16(b["ezview"])                   # h+0x14: mov si,"call EZ-VIEW"
        + b"\xe9" + rel16(h + 0x1A, L)                 # jmp launcher
        + b"\x90\x90\x90"
    )
    assert len(handler) == 29
    return {
        b["handler"]: handler,
        b["str_mov"]: b"\x8b\xc6\x90",                 # mov ax,si ; nop
        b["dispatch"]: b"\xe9" + rel16(b["dispatch"] + 3, h + 0x14),
    }


def context_ok(data, b):
    ds = b["ds"]
    return (data[ds + b["ezview"]:ds + b["ezview"] + 13] == b"call EZ-VIEW\x00"
            and data[ds + b["newline"]:ds + b["newline"] + 2] == b"\n\x00")


def identify(data):
    for ver, b in BUILDS.items():
        if len(data) == b["size"] and context_ok(data, b):
            return ver, b
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst", nargs="?")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    data = bytearray(open(a.src, "rb").read())
    ver, b = identify(data)
    if not ver:
        sys.exit(f"{a.src}: not a known BXX.EXE build (4.79.22 or 4.23); refusing")
    orig, new = original_bytes(b, ver), patched_bytes(b)

    states = {}
    for off in orig:
        cur = bytes(data[off:off + len(orig[off])])
        states[off] = "patched" if cur == new[off] else ("original" if cur == orig[off] else "UNKNOWN")

    print(f"{a.src}: BXX {ver}, md5 {hashlib.md5(data).hexdigest()}")
    for off, st in states.items():
        print(f"  0x{off:05X}: {st}")
    if a.check:
        return
    if not a.dst:
        sys.exit("need an output file name")
    if "UNKNOWN" in states.values():
        sys.exit("unexpected bytes at a patch site; refusing")
    if all(s == "patched" for s in states.values()):
        print("already patched")
    for off, val in new.items():
        data[off:off + len(val)] = val
    open(a.dst, "wb").write(data)
    print(f"wrote {a.dst}, md5 {hashlib.md5(data).hexdigest()}")


if __name__ == "__main__":
    main()
