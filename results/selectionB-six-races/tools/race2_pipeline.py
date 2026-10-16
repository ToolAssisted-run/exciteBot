#!/usr/bin/env python3
"""Race-2 (Excitebike race 1) pipeline on a given start state.  usage: race2_pipeline.py <dir>
<dir> must contain cfg.jaffar (initial sequence = boot + race 1 + idle up to GO-1) and dumpA.ram (throttle-A run from GO).
Stages: (1) collision-free zero-loss line to f1040 (autoline) (2) top-lane landing + A-dip variant (3) lap-1 cluster with
two hits -> 1544 (4) 1544 stretch to f2020 (autoline) (5) lap-2 cluster + finish (STOPWIN) (6) QNES check + clock."""
import sys, subprocess, re, os
R='/home/jaffar/eb-runs'; E='/home/jaffar/jaffarPlus/extern/exciteBot'; D=os.path.abspath(sys.argv[1]); F=2048
BIN=f'{R}/bin/segsearch_e1'; TRAJ=f'{R}/bin/traj_e1'; REF=f'{R}/e1/mov_e1A.sol'; SEED=f'{D}/dumpA.ram'; CFG=os.path.relpath(f'{D}/cfg.jaffar',R)
os.makedirs(f'{D}/logs',exist_ok=True)
def sh(cmd,**kw): return subprocess.run(cmd,shell=True,cwd=R,**kw)
def say(*a): print(*a); sys.stdout.flush()
def search(prefix,end,alpha,env,log):
    e=dict(os.environ,THREADS='220',MASKX='1',MINPRESS='1',MAXSTATES='2000000000',SEEDINPUTS=prefix); e.update(env)
    with open(log,'w') as lf: subprocess.run([BIN,SEED,REF,'bfs','0',str(end),alpha],env=e,stdout=lf,stderr=subprocess.STDOUT,cwd=E)
    return open(log).read()
def extract(log,vel,pin,pout):
    r=subprocess.run(['python3','-I',f'{R}/extract_best.py',log,str(vel),pin,pout],capture_output=True,text=True); say('  ',r.stdout.strip() or r.stderr.strip()[-200:]); return r.returncode==0
def check(tok):
    r=subprocess.run(['python3','-I',f'{R}/e1/check.py',tok,CFG],capture_output=True,text=True,cwd=R); say('  QNES:',r.stdout.strip()); return r.stdout
def traj(tok,end):
    sol=tok[:-4]+'.sol'; subprocess.run(['python3','-I',f'{R}/e1/tok2sol.py',tok,sol,'40'])
    o=subprocess.run([TRAJ,SEED,sol,'0',str(end)],capture_output=True,text=True,cwd=E).stdout
    return {int(r[0]):r for r in (l.split() for l in o.splitlines()) if r and r[0].isdigit()}
# ---- 1. approach
if not os.path.exists(f'{D}/line1040.txt'):
    open(f'{D}/empty.txt','w').write('')
    sh(f"python3 -I autoline.py {BIN} {SEED} {REF} {CFG} {TRAJ} {D}/empty.txt 1040 {D}/line1040.txt 300 full '950-99999:14-37' 0 > {D}/autoline1.out 2>&1")
    say(open(f'{D}/autoline1.out').read()[-400:])
if len(open(f'{D}/line1040.txt').read().split())<1040: say("stage 1 failed"); sys.exit(1)
# ---- 2. landing in the top lane, then the A,A,B dip on the last three ground update frames <= f1073
ok=False
for end,fb in ((1052,'950-99999:14-49'),(1060,'1058-99999:14-49'),(1062,'1060-99999:14-49'),(1058,'1056-99999:14-49')):
    txt=search(f'{D}/line1040.txt',end,'full',{'LOSSMAX':'0','PREFERGROUND':'1','FORBID':fb},f'{D}/logs/land.log')
    m=re.search(r"velX=\s*832 posX=\s*[\d.]+ .*?air=(\d)",txt[txt.find('=== frame'):])
    if m and m.group(1)=='0' and extract(f'{D}/logs/land.log',832,f'{D}/line1040.txt',f'{D}/lineL.txt'): ok=True; Lf=end; break
if not ok: say("stage 2: no grounded top-lane landing found"); sys.exit(1)
base=open(f'{D}/lineL.txt').read().split()
variants={'03':{1065:'A',1069:'A',1073:'B'},'01':{1069:'A',1073:'B'},'02':{1061:'A',1065:'B',1069:'A',1073:'B'}}
done=None
for name,pat in variants.items():
    toks=base+['-']*(1076-len(base))
    for f in range(Lf+1,1077):
        if (f-1)%4==0: toks[f-1]=pat.get(f,'B')
    open(f'{D}/dip_{name}.txt','w').write(' '.join(toks)+'\n')
    rows=traj(f'{D}/dip_{name}.txt',1076)
    if int(rows[1076][4])!=832 or int(rows[1076][3])!=0: say(f"  dip {name}: bad state at f1076 {rows[1076][3:6]}"); continue
    # ---- 3. lap-1 cluster
    txt=search(f'{D}/dip_{name}.txt',1420,'full',{'LOSSMAX':'0','PHASES':'1159:1344:56:1223:84:1400;1227:1544:0'},f'{D}/logs/cluster_{name}.log')   # continue loss-free to f1420 so the chosen exit state is not a dead end
    say(f"  dip {name}:", (re.findall(r"PHASE.*",txt) or ['-'])[0][:60], '|', (re.findall(r"velX=\s*\d+ posX=\s*[\d.]+",txt[txt.find('=== frame'):]) or ['none'])[0])
    if re.search(r"velX=\s*1544",txt[txt.find('=== frame'):]) and extract(f'{D}/logs/cluster_{name}.log',1544,f'{D}/dip_{name}.txt',f'{D}/line1228.txt'): done=name; break
if not done: say("stage 3: no 1544 exit"); sys.exit(1)
c=check(f'{D}/line1228.txt')
if 'contact=[]' not in c or 'mismatches=0' not in c: say("stage 3: contact in the cluster line"); sys.exit(1)
# ---- 4. stretch
sh(f"python3 -I autoline.py {BIN} {SEED} {REF} {CFG} {TRAJ} {D}/line1228.txt 2020 {D}/line2020.txt 300 full '1950-99999:14-37' 1250 > {D}/autoline2.out 2>&1")
say(open(f'{D}/autoline2.out').read()[-300:])
t=open(f'{D}/line2020.txt').read().split()
if len(t)<2020: say("stage 4 failed"); sys.exit(1)
open(f'{D}/line1985.txt','w').write(' '.join(t[:1940])+'\n')   # seed the lap-2 search from f1940 (more approach freedom)
# ---- 5. lap-2 cluster + finish (skip lip 6; counter is saturated from lip 5 on)
txt=search(f'{D}/line1985.txt',2420,'fullA',{'LOSSMAX':'12','PHASES':'2058:1788:12;2102:2044:0','VELXMAX':'2100','STOPWIN':'1','BEAM':'12000000','FORBID':'1950-99999:14-37'},f'{D}/logs/finish.log')
say('  ',(re.findall(r"WIN.*",txt) or ['no WIN'])[0])
m=re.search(r"velX=\s*(\d+) posX",txt[txt.find('=== frame'):])
if not m or not extract(f'{D}/logs/finish.log',int(m.group(1)),f'{D}/line1985.txt',f'{D}/race2_full.txt'): say("stage 5 failed"); sys.exit(1)
# ---- 6. verify
check(f'{D}/race2_full.txt'); say("PIPELINE DONE")
