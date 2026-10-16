#!/bin/bash
# usage: qnes.sh <movie.sol> <dump.ram> [cfg]   (GO-relative movie on the race-2 start state; kills the player once the dump is complete)
cd /home/jaffar/eb-runs; export TERM=xterm; cfg=${3:-e1/cfg_e1.jaffar}; n=$(wc -l < $1); want=$(( (n+1)*2048 )); rm -f $2
/home/jaffar/jaffarPlus/build-eb-nes/jaffar-player $cfg $1 --dumpRam $2 --disableRender --exitOnEnd > /dev/null 2>&1 &
pid=$!
for i in $(seq 1 3600); do sleep 1; kill -0 $pid 2>/dev/null || break; [ -f $2 ] && [ $(stat -c %s $2) -ge $want ] && { sleep 1; kill $pid 2>/dev/null; break; }; done
kill $pid 2>/dev/null; wait $pid 2>/dev/null; ls -la $2 | awk '{print $5, $9}'
