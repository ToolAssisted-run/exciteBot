#!/usr/bin/env python3
"""Append the inputs of the best row with a given velX from a segsearch log to a prefix: extract_best.py <log> <velX> <prefix_in> <prefix_out>"""
import sys, re
log, vel, pin, pout = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
txt = open(log).read()
tab = txt[txt.find('=== frame'):]
m = re.search(r"velX=\s*%d posX=\s*([\d.]+).*?\n  inputs f(\d+)\.\.f(\d+):(.*?)\n" % vel, tab, re.S)
if not m: print("no row with velX", vel); sys.exit(1)
seg = [t.rstrip('*') for t in m.group(4).split()]
toks = open(pin).read().split()
f0 = int(m.group(2))
assert len(toks) == f0 - 1, f"prefix has {len(toks)} tokens but the row starts at f{f0}"
open(pout, 'w').write(' '.join(toks + seg) + '\n')
print(f"velX {vel} posX {m.group(1)}: appended {len(seg)} inputs f{f0}..f{m.group(3)} -> {pout} ({len(toks)+len(seg)} tokens)")
