import struct, sys, numpy as np

# Usage: python3 scripts/verify_signatures.py <path to libminecraftpe.so>
# Prints every full-pattern match for the signatures this mod relies on.
if len(sys.argv) < 2:
    sys.exit('usage: verify_signatures.py <libminecraftpe.so>')
LIB = sys.argv[1]
data = open(LIB, 'rb').read()

# section table (for file-offset -> virtual address)
e_shoff=struct.unpack_from('<Q',data,0x28)[0]; es,en,si=struct.unpack_from('<HHH',data,0x3A)
shstr=struct.unpack_from('<Q',data,e_shoff+si*es+0x18)[0]
secs=[]
for i in range(en):
    o=e_shoff+i*es
    nmoff=struct.unpack_from('<I',data,o)[0]
    addr,off,size=struct.unpack_from('<QQQ',data,o+0x10)
    nm=data[shstr+nmoff:data.index(b'\0',shstr+nmoff)].decode()
    secs.append((nm,addr,off,size))

def va(file_off):
    for nm,addr,off,size in secs:
        if off<=file_off<off+size: return addr+(file_off-off)
    return None

def parse(pattern):
    toks=pattern.split()
    out=[]
    for t in toks:
        out.append(None if t=='?' else int(t,16))
    return out

def match(pattern):
    pat=parse(pattern)
    n=len(pat)
    # anchor on the longest concrete run
    best=(0,0); run=0; start=0
    for i,t in enumerate(pat):
        if t is None:
            if run>best[1]: best=(start,run)
            run=0; start=i+1
        else:
            if run==0: start=i
            run+=1
    if run>best[1]: best=(start,run)
    off,ln=best
    anchor=bytes(pat[off:off+ln])
    buf=data
    needle=anchor
    hits=[]
    pos=0
    while True:
        idx=buf.find(needle, pos)
        if idx<0: break
        pos=idx+1
        cand=idx-off
        if cand<0 or cand+n>len(data): continue
        ok=True
        for j,t in enumerate(pat):
            if t is not None and data[cand+j]!=t: ok=False; break
        if ok: hits.append(cand)
    return hits

pats={
 'CameraBlendSystemTick': "? ? ? D1 ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? 6D ? ? ? A9 ? ? ? F9 ? ? ? A9 ? ? ? A9 ? ? ? 91 55 D0 3B D5 F3 03 01 AA F4 03 00 AA ? ? ? F9 ? ? ? 91 ? ? ? 91",
 'NormalTick (player tick)': "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 ? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39",
}
for name,p in pats.items():
    hits=match(p)
    print(f'{name}: {len(hits)} hit(s)')
    for h in hits:
        print(f'   file {hex(h)}  va {hex(va(h))}')
