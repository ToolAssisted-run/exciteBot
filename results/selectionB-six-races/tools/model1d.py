#!/usr/bin/env python3
"""1D kinematic model of a loss-free Excitebike line: speed, sub-pixel, column counter and boost-lip sections only.
The x-evolution of a zero-loss line depends only on WHEN each lip fires (section pos is initialised to 0x64-1 at the
column entry and the lip fires at pos >= 26), so phase effects of A-press dips can be scanned without the full search.
usage: model1d.py <dumpA.ram> <lipcols comma> [dips: comma list of update indices (0 = first update at full speed)] """
import sys
F=2048
def run(seed, lips, dipset=(), frames=3000, verbose=False, a_cap=800, fin=None):
    m=seed
    cyc=m[0x4C]; vel=0; sub=m[0x394]; c64=m[0x64] if m[0x64]<128 else m[0x64]-256; cum=0; px60=m[0x60]
    sec=False; C8=0; fired=False
    boosts=[]; upd=0; full=False; lipset=set(lips); log=[]; fsec=False; fC8=0; fdone=None
    for f in range(1,frames+1):
        cyc=(cyc+1)&3
        # section machinery (uses previous frame's px)
        if sec:
            C8+=px60
        if not sec and cum in lipset:
            sec=True; C8=c64-1; fired=False; lipset.discard(cum)
            if C8<0: C8+=256
        if sec and not fired and C8>=26:
            vel+=256; fired=True; boosts.append((f,vel,cum,c64)); 
        if sec and fired: sec=False          # (26:00 terminator right after the lip feature)
        if fin and fdone is None:
            if fsec: fC8+=px60
            if not fsec and cum==fin[0]: fsec=True; fC8=(c64-1)&0xFF
            if fsec and fC8>=fin[1]: fdone=f; boosts.append((f,-1,cum,c64))
        # speed step on cycle 0
        if cyc==0 and not [b for b in boosts if b[1]>0]:
            if not full:
                vel=min(832,vel+63)
                if vel==832: full=True; upd=0
            else:
                if upd in dipset:
                    if vel>800: vel-=12          # A above its cap: -12
                else: vel=min(832,vel+63)        # B: back to the cap
                upd+=1
        # posX part 1
        px60=vel>>8; sp=sub+(vel&0xFF); sub=sp&0xFF
        if sp>0xFF: px60+=1
        # column counter
        if px60:
            bd=c64-px60
            if bd<=0: c64=bd+8; cum+=1
            else: c64=bd
        if verbose and f in verbose: log.append((f,vel,cum,c64,sub))
    return boosts, log
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); seed=d[:F]; lips=[int(x) for x in sys.argv[2].split(',')]
    dips=set(int(x) for x in sys.argv[3].split(',')) if len(sys.argv)>3 and sys.argv[3] else set()
    b,_=run(seed,lips,dips)
    for f,v,c,c64 in b: print(f"boost f{f}: -> {v} cum={c} cum-f={c-f} 0x64={c64}")
