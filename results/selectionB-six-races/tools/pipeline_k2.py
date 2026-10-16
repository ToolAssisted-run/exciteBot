#!/usr/bin/env python3
"""Stages 2-4 of pipeline_k for an existing approach prefix (prefix_<tag>_850.txt): continuation to a GROUNDED state
after the last jump, tail derivation (B to keep 832, coast on the last ground update frame before the lip launch,
U on the frame before the launch), then the real-physics pair search.  usage: pipeline_k2.py <tag>"""
import sys, subprocess, re, os
R='/home/jaffar/eb-runs'; tag=sys.argv[1]; pfx=sys.argv[2] if len(sys.argv)>2 else f'{R}/prefix_{tag}_850.txt'   # optional: an earlier prefix (e.g. f800) so the landing is inside the search
def search(prefix, end, extra, log):
    env=dict(os.environ, THREADS='128', MASKX='1', MINPRESS='1', MAXSTATES='600000000', SEEDINPUTS=prefix); env.update(extra)
    with open(log,'w') as lf: subprocess.run([f'{R}/bin/segsearch_t2', f'{R}/dump_t2A.ram', f'{R}/mov_t2A.sol','bfs','0',str(end),'noLR'], env=env, stdout=lf, stderr=subprocess.STDOUT, cwd='/home/jaffar/jaffarPlus/extern/exciteBot')
    txt=open(log).read()
    if 'ABORT' in txt or 'layer empty' in txt: print(f"  {log}: FAILED"); return None
    m=re.search(r"=== frame (\d+).*?\n(velX=.*?)\n  inputs f(\d+)\.\.f\d+:(.*)\n", txt, re.S)
    if not m: print("  parse failure", log); return None
    print("  ", m.group(2)[:100]); return m
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
def replay(toks, end):
    open(f'{R}/mov_{tag}_tmp.sol','w').write(sol(toks+['-']*8))
    o=subprocess.run([f'{R}/bin/traj_t2',f'{R}/dump_t2A.ram',f'{R}/mov_{tag}_tmp.sol','0',str(end)],capture_output=True,text=True).stdout
    return {int(l.split()[0]):l.split() for l in o.splitlines() if l.split() and l.split()[0].isdigit()}
toks=open(pfx).read().split()
# stage 2: continuation to the first grounded state after the jump: try end frames 882..890
grounded=None
for end in (882,884,886,888,890):
    m=search(pfx, end, {'LOSSMAX':'0','FORBID':'0-99999:14-37','PREFERGROUND':'1'}, f'{R}/logs/{tag}_cont{end}.log')
    if m and 'air=0' in m.group(2):
        grounded=end; cont=[t.rstrip('*') for t in m.group(4).split()]; break
if not grounded: print("no grounded continuation found"); sys.exit(1)
toks=toks+cont; print(f"  grounded at f{grounded} (velX in row above)")
# stage 3: tail. Replay with B everywhere to find the lip launch frame F (velX +256)
probe=toks+['B']*12; rows=replay(probe, grounded+12)
F=next((f for f in range(grounded+1, grounded+12) if f in rows and f-1 in rows and int(rows[f][4])-int(rows[f-1][4])>=200), None)
if not F: print("no launch found in the probe"); sys.exit(1)
# ground update frames before F: frames f where state[f-1].cyc==3 (the advance of f is the speed update)
upd=[f for f in range(grounded+1, F) if (f-1) in rows and int(rows[f-1][2])==3]
tail=[]
for f in range(grounded+1, F):
    if f in upd and f==upd[-1]: tail.append('-')      # the coast on the last update frame before the launch -> 776
    elif f in upd: tail.append('B')                    # keep 832 on earlier update frames
    elif f==F-1: tail.append('U')                      # lane trick
    else: tail.append('-')
if F-1 in upd and tail[-1]=='-': tail[-1]='U'          # if the last update frame is F-1: coast must still happen... (U has no throttle bit -> coast + lane press)
toks886=toks+tail; rows=replay(toks886, F+2)
print(f"  launch F={F}, update frames {upd}, tail {tail}; f{F-1}: velX={rows[F-1][4]} b8={rows[F-1][13]} ; f{F}: velX={rows[F][4]} air={rows[F][3]} b8={rows[F][13]} posX={rows[F][5]}")
open(f'{R}/prefix_{tag}_launch.txt','w').write(' '.join(toks886)+'\n')
# stage 4: pair search from F-1
lb=F+56+3
m=search(f'{R}/prefix_{tag}_launch.txt', F+77, {'LOSSMAX':'84','LANDBY':str(lb),'LANDY':'94','LANDVMAX':'1089'}, f'{R}/logs/{tag}_pair.log')
print("done", tag)
