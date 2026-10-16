#!/usr/bin/env python3
"""Set up the next race of the sequence.  usage: mkrace.py <movie_so_far.sol> <lines_to_keep> <dir> <tag>
Finds the next race's clock start / first movable frame, writes <dir>/boot.sol, cfg.jaffar, movA.sol, dumpA.ram (throttle-A run
from GO), source/track_layout_<tag>.hpp, builds bin/segsearch_<tag> and traj_<tag> with the lap thresholds, verifies the engine."""
import sys, json, os, subprocess
R='/home/jaffar/eb-runs'; E='/home/jaffar/jaffarPlus/extern/exciteBot'; F=2048; NL=6
movie, keep, D, tag = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
B='|..|........|'; os.makedirs(f'{R}/{D}/logs',exist_ok=True)
L=open(f'{R}/{movie}').read().splitlines()[:keep]
probe=L+[B]*600+['|..|......B.|']*800   # B held from well before the next clock slot (harmless in the intermission)
open(f'{R}/{D}/mov_go.sol','w').write('\n'.join(probe)+'\n')
subprocess.run([f'{R}/e1/qnes.sh',f'{D}/mov_go.sol',f'{D}/dump_go.ram','t3/cfg_boot.jaffar'],cwd=R,stdout=subprocess.DEVNULL)
d=open(f'{R}/{D}/dump_go.ram','rb').read(); n=len(d)//F
ro=[f for f in range(1,n) if d[f*F+0x52]==1 and d[(f-1)*F+0x52]!=1]
z=next(f for f in range(ro[-1],n) if d[f*F+0x52]==0)
t0=next(f for f in range(z,n) if (d[f*F+0x69],d[f*F+0x6A],d[f*F+0x6B])!=(0,0,0) and (d[f*F+0x6A],d[f*F+0x6B])!=(d[(f-1)*F+0x6A],d[(f-1)*F+0x6B]))
go=next(f for f in range(z,n) if d[f*F+0x94]*256+d[f*F+0x90]>0)
print(f"race overs so far {ro}; next race: clock start f{t0}, first move f{go}, gap {go-t0}")
os.remove(f'{R}/{D}/dump_go.ram')
boot=L+[B]*(go-1-len(L)); open(f'{R}/{D}/boot.sol','w').write('\n'.join(boot)+'\n')
c=json.load(open(f'{R}/cfg_t2A.jaffar')); c['Emulator Configuration']['Initial Sequence File Path']=f'{D}/boot.sol'; c['Driver Configuration']['Max Steps']=80000
json.dump(c,open(f'{R}/{D}/cfg.jaffar','w'),indent=1)
open(f'{R}/{D}/movA.sol','w').write('\n'.join(['|..|.......A|']*14000)+'\n')
subprocess.run([f'{R}/e1/qnes.sh',f'{D}/movA.sol',f'{D}/dumpA.ram',f'{D}/cfg.jaffar'],cwd=R,stdout=subprocess.DEVNULL)
g=open(f'{R}/{D}/dumpA.ram','rb').read(); n=len(g)//F
rov=next((f for f in range(1,n) if g[f*F+0x52]==1),None); crash=next((f for f in range(n) if g[f*F+0x98]),None)
track={}; e0u=0; prev=g[0xE0]; laps=[]; l57=g[0x57]
for f in range(0,(rov or n-1)+1):
    e0u+=(g[f*F+0xE0]-prev)&0x3F; prev=g[f*F+0xE0]; cc=g[f*F+0xE0]
    for lane in range(NL): track[(lane,e0u)]=g[f*F+0x400+lane*0x40+cc]
    if g[f*F+0x57]!=l57: laps.append((f,e0u,l57,g[f*F+0x57])); l57=g[f*F+0x57]
ncol=max(c for (_,c) in track)+1
print(f"A-run: race over f{rov} (clock {g[(rov or 1)*F+0x69]}.{g[(rov or 1)*F+0x6A]:02d}), first contact f{crash}; columns {ncol}; laps {laps}; cum at race over {e0u}")
arr=[[track.get((lane,c),0x3B) for c in range(ncol)] for lane in range(NL)]
lapc=[l[1] for l in laps if l[2]!=0]
for lane in range(NL): print(f"ln{lane}:", [(c,(v-0x40)>>2) for c,v in enumerate(arr[lane]) if c<=(lapc[0] if lapc else ncol)+45 and 0x40<=v and ((v-0x40)>>2)<0x16])
out=["#pragma once",f"// GENERATED from /home/jaffar/eb-runs/{D}/dumpA.ram (throttle-A full run, GO-relative) by mkrace.py.","#include <cstdint>","#include <cstddef>","namespace excitebike {",f"static constexpr size_t kTrackLanes = {NL};",f"static constexpr size_t kTrackColumns = {ncol};","static constexpr uint8_t kTrack[kTrackLanes][kTrackColumns] = {"]
for lane in range(NL): out.append("  {"+",".join(f"0x{v:02X}" for v in arr[lane])+"},")
out+=["};","} // namespace excitebike",""]
open(f'{E}/source/track_layout_{tag}.hpp','w').write("\n".join(out))
l1,l2=(lapc+[9999,9999])[:2]
for t in ('segsearch','traj'):
    r=subprocess.run(f"g++ -O2 -fopenmp -std=c++20 -w -I/home/jaffar/jaffarPlus/emulators/exciteBikeNative -I{E}/source -DEB_TRACK_LAYOUT='\"track_layout_{tag}.hpp\"' -DEB_LAP1={l1} -DEB_LAP2={l2} -o {R}/bin/{t}_{tag} tools/{t}.cpp",shell=True,cwd=E)
    print("built",t,tag,"rc",r.returncode)
v=subprocess.run([f'{R}/bin/segsearch_{tag}',f'{R}/{D}/dumpA.ram',f'{R}/{D}/movA.sol','verifyall','0',str(max(1,(crash or 300)-3))],capture_output=True,text=True,cwd=E).stdout
print(v.strip().splitlines()[-1])
s=open(f'{R}/e1/check.py').read().replace('bin/traj_e1',f'bin/traj_{tag}').replace('e1/dump_e1A.ram',f'{D}/dumpA.ram').replace("'e1/cfg_e1.jaffar'",f"'{D}/cfg.jaffar'")
open(f'{R}/{D}/check.py','w').write(s); open(f'{R}/{D}/empty.txt','w').write('')
