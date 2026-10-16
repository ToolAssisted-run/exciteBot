#!/usr/bin/env python3
"""Chained zero-loss segment search: extend an input prefix in chunks of N frames with exact zero-loss searches
(segsearch_t2, alphabet noLR, MINPRESS, lane limits), taking the best state (highest velX, then posX) of each chunk,
until the state reaches a target cumulative column or a max frame.
usage: chainsearch.py <prefix.txt> <out_prefix.txt> <targetCum> [chunk=200] [forbid="0-99999:14-37"] [lossmax=0]"""
import sys, subprocess, re, os
R='/home/jaffar/eb-runs'; prefix, out, target = sys.argv[1], sys.argv[2], int(sys.argv[3])
chunk=int(sys.argv[4]) if len(sys.argv)>4 else 200; forbid=sys.argv[5] if len(sys.argv)>5 else "0-99999:14-37"; lossmax=sys.argv[6] if len(sys.argv)>6 else "0"
toks=open(prefix).read().split()
while True:
    n=len(toks); end=n+chunk; tmp=f'{R}/chain_tmp.txt'; open(tmp,'w').write(' '.join(toks)+'\n')
    env=dict(os.environ, THREADS='128', SEEDINPUTS=tmp, LOSSMAX=lossmax, MASKX='1', MINPRESS='1', FORBID=forbid, MAXSTATES='600000000')
    log=f'{R}/logs/chain_{n}_{end}.log'
    with open(log,'w') as lf: subprocess.run([f'{R}/bin/segsearch_t2', f'{R}/dump_t2A.ram', f'{R}/mov_t2A.sol', 'bfs', '0', str(end), 'noLR'], env=env, stdout=lf, stderr=subprocess.STDOUT, cwd='/home/jaffar/jaffarPlus/extern/exciteBot')
    txt=open(log).read()
    if 'ABORT' in txt or 'layer empty' in txt: print(f"chunk {n}->{end}: FAILED ({'ABORT' if 'ABORT' in txt else 'empty'}); see {log}"); break
    m=re.search(r"=== frame (\d+).*?\nvelX=\s*(\d+) posX=\s*([\d.]+).*?cum=(\d+)\n  inputs f\d+\.\.f\d+:(.*?)\n", txt, re.S)
    if not m: print("parse failure", log); break
    seg=[t.rstrip('*') for t in m.group(5).split()]; seg=seg[-chunk:] if len(seg)>chunk else seg
    toks+=seg; cum=int(m.group(4)); print(f"chunk {n}->{end}: velX={m.group(2)} posX={m.group(3)} cum={cum} (+{len(seg)} inputs)"); sys.stdout.flush()
    open(out,'w').write(' '.join(toks)+'\n')
    if cum>=target: print("target reached"); break
    if end>4000: print("max frames"); break
