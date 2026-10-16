#!/usr/bin/env python3
"""Phase scan by total shift: k separated A-dips (each -0.1875 scroll px) for k = 0..K.  usage: scan2.py <dumpA.ram> <lips> <satIndex> [K=40]"""
import sys
sys.path.insert(0,'/home/jaffar/eb-runs/phase')
from model1d import run
dump, lips, sat = sys.argv[1], [int(x) for x in sys.argv[2].split(',')], int(sys.argv[3]); K=int(sys.argv[4]) if len(sys.argv)>4 else 40
seed=open(dump,'rb').read()[:2048]
for k in range(0,K+1):
    dips=set(range(0,2*k,2)); b,_=run(seed,lips,dips,frames=3200)
    if len(b)<len(lips): print(k,"lips missed"); continue
    f,v,c,c64=b[sat]; print(f"k={k:2d} dips: boost frames {[x[0] for x in b]}  saturation cum-f={c-f} px-left={c64}")
