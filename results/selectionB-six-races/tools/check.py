#!/usr/bin/env python3
"""QNES check of a GO-relative token line on the real race 2: check.py <tokens.txt> [cfg] -> first velX mismatch vs engine, contact frames, race over, clock"""
import sys, subprocess, os
R='/home/jaffar/eb-runs'; F=2048; tk=sys.argv[1]; cfg=sys.argv[2] if len(sys.argv)>2 else 'e1/cfg_e1.jaffar'
base=os.path.splitext(tk)[0]; sol=base+'.sol'; dump=base+'.ram'
subprocess.run(['python3','-I',f'{R}/e1/tok2sol.py',tk,sol,'30'])
subprocess.run([f'{R}/e1/qnes.sh',sol,dump,cfg],stdout=subprocess.DEVNULL)
d=open(dump,'rb').read(); n=len(d)//F
o=subprocess.run([f'{R}/bin/traj_e1',f'{R}/e1/dump_e1A.ram',sol,'0',str(n-2)],capture_output=True,text=True).stdout
rows={int(r[0]):r for r in (l.split() for l in o.splitlines()) if r and r[0].isdigit()}
ro=next((f for f in range(1,n) if d[f*F+0x52]==1),None); lim=ro or n-1
mism=[f for f in range(1,lim) if f in rows and int(rows[f][4])!=d[f*F+0x94]*256+d[f*F+0x90]]
cr=[f for f in range(1,lim) if d[f*F+0x98]!=0]
m=d[lim*F:(lim+1)*F]
print(f"frames={n} velX mismatches={len(mism)} first={mism[:3]} contact={cr[:3]} ({len(cr)}) RaceOver={ro} clock={m[0x69]}.{m[0x6A]:02d}")
if cr:
    f=cr[0]; mm=d[f*F:(f+1)*F]; print(f"  contact f{f}: player b8={mm[0xB8]} posY={mm[0x8C]} air={mm[0xB0]} | opp posY {mm[0x8D]},{mm[0x8E]},{mm[0x8F]} opp b8 {mm[0xB9]},{mm[0xBA]},{mm[0xBB]}")
