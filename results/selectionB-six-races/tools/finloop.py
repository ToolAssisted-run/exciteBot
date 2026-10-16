#!/usr/bin/env python3
"""Final segment (lips + finish) with QNES in the loop.  usage: finloop.py <dir> <tag> <seed_tokens> <out_tokens> <PHASES> [beam=12000000] [alpha=full] [lossmax=0] [end=3200]
Reruns the STOPWIN search with a growing narrow FORBID list until the complete line is contact-free on QuickerNES."""
import subprocess, re, os, sys
R='/home/jaffar/eb-runs'; E='/home/jaffar/jaffarPlus/extern/exciteBot'
D,tag,seed,out,phases=sys.argv[1:6]; beam=sys.argv[6] if len(sys.argv)>6 else '12000000'; alpha=sys.argv[7] if len(sys.argv)>7 else 'full'; lossmax=sys.argv[8] if len(sys.argv)>8 else '0'; end=sys.argv[9] if len(sys.argv)>9 else '3200'
forb=[]
for it in range(1,16):
    env=dict(os.environ,THREADS='230',STOPWIN='1',BEAM=beam,SEEDINPUTS=f'{R}/{seed}',LOSSMAX=lossmax,MASKX='1',MINPRESS='1',MAXSTATES='2000000000')
    if phases!='-': env['PHASES']=phases
    if forb: env['FORBID']=';'.join(f"{a}-{b}:{lo}-{hi}" for a,b,lo,hi in forb)
    log=f'{R}/{D}/logs/finloop_{it}.log'
    with open(log,'w') as lf: subprocess.run([f'{R}/bin/segsearch_{tag}',f'{R}/{D}/dumpA.ram',f'{R}/{D}/movA.sol','bfs','0',end,alpha],env=env,stdout=lf,stderr=subprocess.STDOUT,cwd=E)
    txt=open(log).read(); w=re.search(r"WIN: \d+ states with race over at f(\d+)",txt)
    if not w: print(f"[{it}] no WIN ({(re.findall(r'layer empty.*|ABORT.*',txt) or ['?'])[0]}); phases: {re.findall(r'PHASE at.*',txt)}; forbids {forb}"); break
    tab=txt[txt.find('=== frame'):]; v=re.search(r"velX=\s*(\d+) posX",tab).group(1)
    subprocess.run(['python3','-I',f'{R}/extract_best.py',log,v,f'{R}/{seed}',f'{R}/{D}/fin_try.txt'],capture_output=True,text=True)
    c=subprocess.run(['python3','-I',f'{R}/{D}/check.py',f'{R}/{D}/fin_try.txt'],capture_output=True,text=True,cwd=R).stdout
    m=re.search(r"contact=\[(\d+)",c); mm=re.search(r"first=\[(\d+)",c); ro=re.search(r"RaceOver=(\w+) clock=([\d.]+)",c)
    print(f"[{it}] engine WIN f{w.group(1)} velX {v} | QNES: {c.strip().splitlines()[0][:110]}"); sys.stdout.flush()
    if not m and not mm:
        os.replace(f'{R}/{D}/fin_try.txt',f'{R}/{out}'); print("CLEAN", ro.group(1), ro.group(2)); break
    f=int(m.group(1)) if m else int(mm.group(1))
    pb=re.search(r"player b8=(\d+)",c)
    if pb: b8=int(pb.group(1))
    else:
        d=open(f'{R}/{D}/fin_try.ram','rb').read(); b8=d[f*2048+0xB8]
    forb.append((f-6,f+6,max(0,b8-4),b8+4))
print("FINLOOP DONE")
