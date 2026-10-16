#!/usr/bin/env python3
"""Re-derive the whole lap-1 line after an early perturbation (k restorable coasts at the first ground update frames
of the approach): exact zero-loss approach to f850 -> continuation to f880 -> realized tail (landing, coast to 776, U)
-> real-physics pair search (<=3 flight hits). Prints each stage's best row.  usage: pipeline_k.py <k>"""
import sys, subprocess, re, os
R='/home/jaffar/eb-runs'; k=int(sys.argv[1]); tag=f'k{k}'
pre=open(f'{R}/approach3.txt').read().split()
coasts=[61,65,69][:k]                         # consecutive restorable ground update frames (B at f61,65,69 ... all grounded)
p=list(pre)
for cf in coasts: p[cf-1]='-'
start=coasts[-1]+4                            # the B after the last coast restores 832; search from there
open(f'{R}/prefix_{tag}_start.txt','w').write(' '.join(p[:start])+'\n')
def search(prefix, end, extra, log):
    env=dict(os.environ, THREADS='128', MASKX='1', MINPRESS='1', MAXSTATES='600000000'); env.update(extra)
    env['SEEDINPUTS']=prefix
    with open(log,'w') as lf: subprocess.run([f'{R}/bin/segsearch_t2', f'{R}/dump_t2A.ram', f'{R}/mov_t2A.sol','bfs','0',str(end),'noLR'], env=env, stdout=lf, stderr=subprocess.STDOUT, cwd='/home/jaffar/jaffarPlus/extern/exciteBot')
    txt=open(log).read()
    if 'ABORT' in txt or 'layer empty' in txt: print(f"  {log}: FAILED"); return None
    m=re.search(r"=== frame (\d+).*?\n(velX=.*?)\n  inputs f(\d+)\.\.f\d+:(.*)\n", txt, re.S)
    if not m: print("  parse failure", log); return None
    print("  ", m.group(2)[:90]); return m
# stage 1: zero-loss approach to f850
m=search(f'{R}/prefix_{tag}_start.txt', 850, {'LOSSMAX':'0','FORBID':'80-850:14-37'}, f'{R}/logs/{tag}_approach.log')
if not m: sys.exit(1)
toks=p[:start]+[t.rstrip('*') for t in m.group(4).split()]; assert len(toks)==850, len(toks)
open(f'{R}/prefix_{tag}_850.txt','w').write(' '.join(toks)+'\n')
# stage 2: continuation to f880 (still airborne in the last jump)
m=search(f'{R}/prefix_{tag}_850.txt', 880, {'LOSSMAX':'0','FORBID':'0-99999:14-37'}, f'{R}/logs/{tag}_cont.log')
if not m: sys.exit(1)
toks+= [t.rstrip('*') for t in m.group(4).split()]; assert len(toks)==880
# stage 3: realized tail (same as line963): R (air update, no hit), L (landing), -, -, - (coast), U
toks886=toks+['R','L','-','-','-','U']; open(f'{R}/prefix_{tag}_886.txt','w').write(' '.join(toks886)+'\n')
def sol(tt):
    o=[]
    for t in tt:
        s=list('|..|........|')
        for c in t:
            if c=='U': s[4]='U'
            if c=='D': s[5]='D'
            if c=='L': s[6]='L'
            if c=='R': s[7]='R'
            if c=='B': s[10]='B'
        o.append(''.join(s))
    return '\n'.join(o)+'\n'
open(f'{R}/mov_{tag}_886.sol','w').write(sol(toks886+['-']*4))
o=subprocess.run([f'{R}/bin/traj_t2',f'{R}/dump_t2A.ram',f'{R}/mov_{tag}_886.sol','0','890'],capture_output=True,text=True).stdout
rows={int(l.split()[0]):l.split() for l in o.splitlines() if l.split() and l.split()[0].isdigit()}
for f in (880,882,885,886,887,888):
    if f in rows: print(f"  f{f}: air={rows[f][3]} velX={rows[f][4]} posX={rows[f][5]} b8={rows[f][13]}")
# stage 4: pair search
m=search(f'{R}/prefix_{tag}_886.txt', 964, {'LOSSMAX':'84','LANDBY':'948','LANDY':'94','LANDVMAX':'1089'}, f'{R}/logs/{tag}_pair.log')
print("done", tag)
