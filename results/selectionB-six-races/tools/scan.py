#!/usr/bin/env python3
"""Phase scan with the 1D model.  usage: scan.py <tag> <dumpA.ram> <lips taken: comma list of table columns> <satLipIndex or -1> <finishCol>"""
import sys, re, itertools
sys.path.insert(0,'/home/jaffar/eb-runs/phase')
from model1d import run
tag, dump, lips, sat, fin = sys.argv[1], sys.argv[2], [int(x) for x in sys.argv[3].split(',')], int(sys.argv[4]), int(sys.argv[5])
seed=open(dump,'rb').read()[:2048]
def metric(dips):
    b,_=run(seed,lips,dips,frames=3200)
    if len(b)<len(lips): return None
    if sat>=0:
        f,v,c,c64=b[sat]; return (c-f, -c64, [x[0] for x in b])     # higher cum-f is better; then fewer px left in the column
    # unsaturated finish: frame at which cum reaches fin (simulate on)
    b2,log=run(seed,lips,dips,frames=3200,verbose=set(range(1,3201)))
    ff=next((f for (f,vel,cum,c64,sub) in log if cum>=fin),None)
    return (-ff, 0, [x[0] for x in b])
base=metric(set()); print("baseline:",base)
best=[]
# dip patterns: any subset of the first N full-speed update frames such that consecutive A presses are allowed (820, 808, ...)
N=14
for k in range(0,5):
    for combo in itertools.combinations(range(N),k):
        m=metric(set(combo))
        if m and (m[0],m[1])>(base[0],base[1]): best.append((m[0],m[1],combo,m[2]))
best.sort(reverse=True)
print(len(best),"patterns better than baseline; top:")
for x in best[:12]: print("  cum-f / -frame:",x[0],"px-left:",-x[1],"dips at updates",x[2],"boost frames",x[3])
