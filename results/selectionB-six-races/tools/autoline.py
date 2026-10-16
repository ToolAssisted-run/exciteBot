#!/usr/bin/env python3
"""Collision-free zero-loss line builder with QuickerNES in the loop.
usage: autoline.py <bin> <seed.ram> <ref.sol> <cfg.jaffar> <traj_bin> <prefix_tokens> <target_frame> <out_tokens> [chunk=300] [alpha=full] [baseforbid=""] [keep=0]
 - extends the token prefix with exact zero-loss searches (window = chunk), replays every candidate on QNES,
   and on an opponent contact at frame c adds FORBID (c-15..c+15, player b8 +-7) and cuts the line back to c-80.
 - keep = number of leading tokens that may never be cut."""
import sys, subprocess, re, os
R='/home/jaffar/eb-runs'; E='/home/jaffar/jaffarPlus/extern/exciteBot'; F=2048
binp, seed, ref, cfg, trajb, prefix, target, out = sys.argv[1:9]; target=int(target)
chunk=int(sys.argv[9]) if len(sys.argv)>9 else 300; alpha=sys.argv[10] if len(sys.argv)>10 else 'full'
basefb=sys.argv[11] if len(sys.argv)>11 else ''; keep=int(sys.argv[12]) if len(sys.argv)>12 else 0
tag=os.path.splitext(os.path.basename(out))[0]; W=os.path.dirname(os.path.abspath(out)); os.makedirs(f'{W}/logs',exist_ok=True)
toks=open(prefix).read().split()
forb=[]
def fbstr(): return ';'.join([basefb]*(1 if basefb else 0)+[f"{a}-{b}:{lo}-{hi}" for a,b,lo,hi in forb])
def search(tt,end,it):
    tmp=f'{W}/{tag}_seed.txt'; open(tmp,'w').write(' '.join(tt)+'\n'); log=f'{W}/logs/{tag}_{it}_{len(tt)}_{end}.log'
    env=dict(os.environ,THREADS='200',SEEDINPUTS=tmp,LOSSMAX='0',MASKX='1',MINPRESS='1',MAXSTATES='1500000000')
    if fbstr(): env['FORBID']=fbstr()
    with open(log,'w') as lf: subprocess.run([binp,seed,ref,'bfs','0',str(end),alpha],env=env,stdout=lf,stderr=subprocess.STDOUT,cwd=E)
    txt=open(log).read()
    if 'ABORT' in txt or 'layer empty' in txt:
        m=re.search(r"(layer empty at f(\d+)|ABORT at f(\d+))",txt); return None, (int(m.group(2) or m.group(3)) if m else None)
    tab=txt[txt.find('=== frame'):]
    m=re.search(r"velX=\s*(\d+) posX=\s*([\d.]+).*?\n  inputs f(\d+)\.\.f(\d+):(.*?)\n",tab,re.S)
    if not m: return None, None
    return [t.rstrip('*') for t in m.group(5).split()], (int(m.group(1)),float(m.group(2)))
def qnes(tt):
    sol=f'{W}/{tag}_cand.sol'; dump=f'{W}/{tag}_cand.ram'; open(f'{W}/{tag}_cand.txt','w').write(' '.join(tt)+'\n')
    subprocess.run(['python3','-I',f'{R}/e1/tok2sol.py',f'{W}/{tag}_cand.txt',sol,'4'])
    subprocess.run([f'{R}/e1/qnes.sh',sol,dump,cfg],stdout=subprocess.DEVNULL,cwd=R)
    d=open(dump,'rb').read(); n=len(d)//F
    o=subprocess.run([trajb,seed,sol,'0',str(len(tt))],capture_output=True,text=True,cwd=E).stdout
    rows={int(r[0]):r for r in (l.split() for l in o.splitlines()) if r and r[0].isdigit()}
    for f in range(1,min(n,len(tt)+1)):
        q=d[f*F+0x94]*256+d[f*F+0x90]
        if d[f*F+0x98]!=0 or (f in rows and int(rows[f][4])!=q):
            return f, d[f*F+0xB8], d[f*F+0x98]
    return None
it=0; backoff=0
while len(toks)<target and it<200:
    it+=1; end=min(len(toks)+chunk+backoff, target)
    seg,info=search(toks,end,it)
    if seg is None:
        cut=max(keep, len(toks)-150)
        print(f"[{it}] search f{len(toks)}->f{end} FAILED at {info}; cutting back to f{cut}"); sys.stdout.flush()
        if cut==len(toks): print("cannot cut further: FAIL"); sys.exit(1)
        toks=toks[:cut]; backoff=min(backoff+150,600); continue
    cand=toks+seg; c=qnes(cand)
    if c is None:
        commit=len(cand) if end>=target else max(len(toks), len(cand)-100)
        toks=cand[:commit]; backoff=0
        print(f"[{it}] f{len(cand)-len(seg)}->f{end} clean on QNES ({info}); committed to f{commit}"); sys.stdout.flush()
        open(out,'w').write(' '.join(toks)+'\n')
    else:
        f,b8,flag=c; BF=int(os.environ.get('ABAND_F','15')); BB=int(os.environ.get('ABAND_B8','7')); forb.append((f-BF,f+BF,max(0,b8-BB),b8+BB)); cut=max(keep, min(len(toks), f-80))
        print(f"[{it}] f{len(cand)-len(seg)}->f{end}: QNES divergence/contact at f{f} (player b8={b8}, flag={flag}); FORBID now {fbstr()}; cut back to f{cut}"); sys.stdout.flush()
        toks=cand[:cut] if cut<=len(cand) else cand
        toks=toks[:cut]
print("DONE" if len(toks)>=target else "STOPPED", len(toks), "tokens; forbids:", fbstr()); open(out,'w').write(' '.join(toks)+'\n')
