import sys, capstone, struct
d=open(__import__('os').path.join(__import__('os').path.dirname(__file__),'..','..','BMDC.BIN'),'rb').read()
md=capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_M68K_040)
a,end=int(sys.argv[1],16),int(sys.argv[2],16)
def ext(b):
    se=struct.unpack('>H',b[:2])[0]; mant=struct.unpack('>Q',b[4:12])[0]
    if mant==0: return 0.0
    return (-1 if se&0x8000 else 1)*mant/2**63*2.0**((se&0x7fff)-16383)

# 68882 general instructions (cpid 1, F200-F23F) decoded here, because capstone gets the
# length of abs.w/abs.l/(d16,An)/#imm operands wrong and then loses sync, and misdecodes FBcc.
FMT = {0: ('l', 4), 1: ('s', 4), 2: ('x', 12), 3: ('p', 12), 4: ('w', 2), 5: ('d', 8), 6: ('b', 2)}
FOPS = {0x00: 'fmove', 0x01: 'fint', 0x02: 'fsinh', 0x03: 'fintrz', 0x04: 'fsqrt', 0x06: 'flognp1',
        0x08: 'fetoxm1', 0x09: 'ftanh', 0x0a: 'fatan', 0x0c: 'fasin', 0x0d: 'fatanh', 0x0e: 'fsin',
        0x0f: 'ftan', 0x10: 'fetox', 0x11: 'ftwotox', 0x12: 'ftentox', 0x14: 'flogn', 0x15: 'flog10',
        0x16: 'flog2', 0x18: 'fabs', 0x19: 'fcosh', 0x1a: 'fneg', 0x1c: 'facos', 0x1d: 'fcos',
        0x1e: 'fgetexp', 0x1f: 'fgetman', 0x20: 'fdiv', 0x21: 'fmod', 0x22: 'fadd', 0x23: 'fmul',
        0x24: 'fsgldiv', 0x25: 'frem', 0x26: 'fscale', 0x27: 'fsglmul', 0x28: 'fsub', 0x38: 'fcmp',
        0x3a: 'ftst'}
FCC = ['f', 'eq', 'ogt', 'oge', 'olt', 'ole', 'ogl', 'or', 'un', 'ueq', 'ugt', 'uge', 'ult', 'ule',
       'ne', 't', 'sf', 'seq', 'gt', 'ge', 'lt', 'le', 'gl', 'gle', 'ngle', 'ngl', 'nle', 'nlt', 'nge',
       'ngt', 'sne', 'st']

def fea(op, at, size):
    """Decode the EA of an F-line word at `at` (extension words start at `at`). -> (text, nbytes)"""
    mode, reg = (op >> 3) & 7, op & 7
    if mode == 0: return 'd%d' % reg, 0
    if mode == 2: return '(a%d)' % reg, 0
    if mode == 3: return '(a%d)+' % reg, 0
    if mode == 4: return '-(a%d)' % reg, 0
    if mode == 5: return '%d(a%d)' % (struct.unpack('>h', d[at:at+2])[0], reg), 2
    if mode == 7 and reg == 0: return '$%x.w' % (struct.unpack('>h', d[at:at+2])[0] & 0xffffffff), 2
    if mode == 7 and reg == 1: return '$%x.l' % struct.unpack('>I', d[at:at+4])[0], 4
    if mode == 7 and reg == 4:
        b = d[at:at+size]
        if size == 12: v = repr(ext(b))
        elif size == 8: v = repr(struct.unpack('>d', b)[0])
        elif size == 4 and FMT_cur == 's': v = repr(struct.unpack('>f', b)[0])
        else: v = '$%x' % int.from_bytes(b, 'big', signed=True)
        return '#' + v, size
    return None, 0

def fpu(at):
    """Return (text, length) for an F-line instruction we handle, else None."""
    global FMT_cur
    op, ex = struct.unpack('>HH', d[at:at+4])
    if (op & 0xffc0) in (0xf280, 0xf2c0):                         # FBcc.w / FBcc.l
        if op & 0x40:
            disp, n = struct.unpack('>i', d[at+2:at+6])[0], 6
        else:
            disp, n = struct.unpack('>h', d[at+2:at+4])[0], 4
        return 'fb%-8s $%x' % (FCC[op & 0x1f], at + 2 + disp), n
    if (op & 0xffc0) != 0xf200: return None
    cls, rm, src, dst, opm = ex >> 13, (ex >> 14) & 1, (ex >> 10) & 7, (ex >> 7) & 7, ex & 0x7f
    if cls == 0:                                                  # fp-fp
        name = FOPS.get(opm)
        if not name: return None
        return '%-9s fp%d, fp%d' % (name, src, dst), 4
    if cls == 2 and src == 7:                                     # fmovecr
        return 'fmovecr   #$%x, fp%d' % (opm, dst), 4
    if cls in (2, 3):
        fmt, size = FMT[src] if src in FMT else ('?', 0)
        FMT_cur = fmt
        t, n = fea(op, at + 4, size)
        if t is None: return None
        if cls == 2:
            name = FOPS.get(opm)
            if not name: return None
            return '%-9s %s, fp%d' % (name + '.' + fmt, t, dst), 4 + n
        return '%-9s fp%d, %s' % ('fmove.' + fmt, dst, t), 4 + n
    return None

FMT_cur = ''
while a<end:
    r = fpu(a) if a + 4 <= len(d) else None
    if r:
        print(f"{a:06x}  {r[0]}"); a += r[1]; continue
    ins = next(md.disasm(d[a:end], a, 1), None)
    if ins is None:
        print(f"{a:06x}  ?? {d[a:a+2].hex()}"); a+=2; continue
    print(f"{ins.address:06x}  {ins.mnemonic:9s} {ins.op_str}"); a=ins.address+ins.size
