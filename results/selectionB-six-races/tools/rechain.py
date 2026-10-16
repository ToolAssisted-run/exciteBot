#!/usr/bin/env python3
"""Carry a track-2 input prefix (tokens, frames 1..N) to the finish with exact zero-loss chunk searches (merged windows
on failure), then verify on QuickerNES and read the in-game clock.
usage: rechain.py <prefix_tokens.txt> <tag> [chunk=400] [forbid=0-99999:14-37]"""
import sys, subprocess, re, os, time
R='/home/jaffar/eb-runs'; E='/home/jaffar/jaffarPlus/extern/exciteBot'; P='/home/jaffar/jaffarPlus/build-eb-nes/jaffar-player'
prefix, tag = sys.argv[1], sys.argv[2]
chunk = int(sys.argv[3]) if len(sys.argv)>3 else 400
forbid = sys.argv[4] if len(sys.argv)>4 else "0-99999:14-37"
cfg = sys.argv[5] if len(sys.argv)>5 else 'cfg_t2A.jaffar'
alpha = sys.argv[6] if len(sys.argv)>6 else 'full'
cap = int(sys.argv[7]) if len(sys.argv)>7 else 2400   # never search far past the race over (post-race deceleration is a 'loss')   # ground leans needed: a takeoff on an update frame costs 28 unless R is held
toks = open(prefix).read().split()
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
            if c=='A': s[11]='A'
        o.append(''.join(s))
    return '\n'.join(o)+'\n'
def search(tt, end, lossmax, log, al=None):
    al = al or alpha
    tmp=f'{R}/rechain_{tag}_seed.txt'; open(tmp,'w').write(' '.join(tt)+'\n')
    env=dict(os.environ, THREADS='128', SEEDINPUTS=tmp, LOSSMAX=str(lossmax), MASKX='1', MINPRESS='1', FORBID=forbid, MAXSTATES='600000000')
    with open(log,'w') as lf: subprocess.run([f'{R}/bin/segsearch_t2', f'{R}/dump_t2A.ram', f'{R}/mov_t2A.sol','bfs','0',str(end),al], env=env, stdout=lf, stderr=subprocess.STDOUT, cwd=E)
    txt=open(log).read()
    if 'ABORT' in txt or 'layer empty' in txt: return None
    m=re.search(r"=== frame (\d+).*?\nvelX=\s*(\d+) posX=\s*([\d.]+).*?cum=(\d+)\n  inputs f(\d+)\.\.f\d+:(.*?)\n", txt, re.S)
    if not m: return None
    seg=[t.rstrip('*') for t in m.group(6).split()]
    return dict(velX=int(m.group(2)), posX=float(m.group(3)), cum=int(m.group(4)), seg=seg)
# ---- stage 1: chain to the finish ----
while True:
    n=len(toks); done=False
    for (c,lm,al) in ((chunk,0,alpha),(chunk+200,0,alpha),(chunk+400,0,alpha),(chunk,12,'fullA'),(chunk+200,12,'fullA'),(chunk,28,alpha),(chunk+200,28,alpha)):
        end=min(n+c, cap)
        if end<=n+40: continue
        log=f'{R}/logs/rechain_{tag}_{n}_{end}_L{lm}.log'
        r=search(toks, end, lm, log, al)
        if r:
            toks=toks+r['seg'][-(end-n):] if len(r['seg'])>=end-n else toks+r['seg']
            print(f"chunk f{n}->f{end} (lossmax {lm}): velX={r['velX']} posX={r['posX']:.3f} cum={r['cum']}"); sys.stdout.flush()
            open(f'{R}/rechain_{tag}_prefix.txt','w').write(' '.join(toks)+'\n'); done=True
            if r['cum']>=1250: print("finish reached"); break
            break
        else: print(f"chunk f{n}->f{end} (lossmax {lm}): FAILED ({log})"); sys.stdout.flush()
    if not done: print("chain stuck at f", n); sys.exit(1)
    if r['cum']>=1250 or len(toks)>3000: break
# ---- stage 2: QNES verify ----
mov=f'{R}/rechain_{tag}.sol'; open(mov,'w').write(sol(toks+['-']*10)); dump=f'{R}/rechain_{tag}.ram'
if os.path.exists(dump): os.remove(dump)
p=subprocess.Popen([P,cfg,mov,'--dumpRam',dump,'--disableRender','--exitOnEnd'],cwd=R,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,env=dict(os.environ,TERM='xterm'))
t0=time.time(); last=-1
while time.time()-t0<900:
    time.sleep(5)
    if os.path.exists(dump):
        sz=os.path.getsize(dump)
        if sz==last and sz>0: break
        last=sz
p.kill()
d=open(dump,'rb').read(); F=2048; n=len(d)//F
ro=next((f for f in range(1,n) if d[f*F+0x52]==1),None)
m=d[(ro or n-1)*F:(ro or n-1)*F+F]
print(f"QNES: frames={n} RaceOver@f{ro} in-game {m[0x69]}.{m[0x6A]:02d} s  (Tom 37.84; my previous 37.95)")
# per-frame velX comparison engine vs QNES up to race over
o=subprocess.run([f'{R}/bin/traj_t2',f'{R}/dump_t2A.ram',mov,'0',str((ro or n-1)-1)],capture_output=True,text=True).stdout
rows={int(r[0]):r for r in (l.split() for l in o.splitlines()) if r and r[0].isdigit()}
mism=[f for f in range(1,(ro or n-1)) if f in rows and int(rows[f][4])!=d[f*F+0x94]*256+d[f*F+0x90]]
print("velX mismatches engine vs QNES:",len(mism), mism[:5])
crash=[f for f in range(1,(ro or n-1)) if d[f*F+0x98]!=0]
print("QNES crash/contact frames:", crash[:10])
