#!/bin/bash
# Replay a GO-relative race .sol on the zero-gap boot (s5) and the one-gap boots (s15,17,18,19); print race over, clock, contact.
R=/home/jaffar/eb-runs; P=/home/jaffar/jaffarPlus/build-eb-nes/jaffar-player; cd $R; export TERM=xterm; sol=$1
for s in 5 15 17 18 19 3 6 7 8; do
  cfg=tom/cfg_s$s.jaffar; [ $s = 5 ] && cfg=cfg_t2_s5.jaffar
  timeout 120 $P $cfg $sol --dumpRam tom/dump_clock_s$s.ram --disableRender --exitOnEnd > /dev/null 2>&1
  python3 -I - $s <<'PY'
import sys,os
R='/home/jaffar/eb-runs'; F=2048; s=int(sys.argv[1]); p=f'{R}/tom/dump_clock_s{s}.ram'
if not os.path.exists(p): print(f"s={s}: no dump"); sys.exit()
d=open(p,'rb').read(); n=len(d)//F
ro=next((f for f in range(1,n) if d[f*F+0x52]==1),None); cr=[f for f in range(1,n) if d[f*F+0x98]!=0]
m=d[(ro if ro else n-1)*F:(ro if ro else n-1)*F+F]
print(f"boot s={s:2d}: RaceOver@{ro} clock={m[0x69]}.{m[0x6A]:02d} sub={m[0x6B]} contact={cr[:3]} ({len(cr)})")
PY
done
