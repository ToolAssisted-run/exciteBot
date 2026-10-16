#!/usr/bin/env python3
"""Build a race-2 seed dir from a race-1 boot: mkseed2.py <boot.sol> <dir> [go=3956]"""
import sys, json, os, subprocess
R='/home/jaffar/eb-runs'; boot=open(sys.argv[1]).read().splitlines(); D=sys.argv[2]; go=int(sys.argv[3]) if len(sys.argv)>3 else 3956
race=open('/home/jaffar/jaffarPlus/extern/exciteBot/reference/race01.sol').read().splitlines()
os.makedirs(f'{R}/{D}',exist_ok=True)
mov=boot+race; mov=mov+['|..|........|']*(go-1-len(mov))
open(f'{R}/{D}/boot.sol','w').write('\n'.join(mov)+'\n')
c=json.load(open(f'{R}/cfg_t2A.jaffar')); c['Emulator Configuration']['Initial Sequence File Path']=f'{D}/boot.sol'; c['Driver Configuration']['Max Steps']=40000
json.dump(c,open(f'{R}/{D}/cfg.jaffar','w'),indent=1)
subprocess.run([f'{R}/e1/qnes.sh','e1/mov_e1A.sol',f'{D}/dumpA.ram',f'{D}/cfg.jaffar'],cwd=R,stdout=subprocess.DEVNULL)
d=open(f'{R}/{D}/dumpA.ram','rb').read(); F=2048
print(D,"boot lines",len(mov),"| frame0 clock",d[0x69],d[0x6A],d[0x6B],"cyc",d[0x4C],"| f1 velX",d[F+0x94]*256+d[F+0x90],"clock@f1",d[F+0x6A],d[F+0x6B],"| track",d[0x42],"mode",d[0x46])
