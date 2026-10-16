#!/usr/bin/env python3
"""Convert an FCEUX .fm2 movie (port-0 inputs 'RLDUTSBA') to the raw per-frame .sol format used by the
reference kit ('|..|UDLRSsBA|' style, one line per frame). The exact pressed-button characters for the
Start/Select slots are learned from a reference .sol whose first frames correspond to the fm2's first frames.
Usage: fm2_to_sol.py <movie.fm2> <reference.sol> <out.sol>   (prints the alignment check)"""
import sys
fm2, ref, out = sys.argv[1:4]
frames = [l.rstrip('\n') for l in open(fm2, encoding='latin-1') if l.startswith('|')]
subs = [l.rstrip('\n') for l in open(fm2, encoding='latin-1') if l.startswith('subtitle')]
refl = [l.rstrip('\n') for l in open(ref) if l.startswith('|')]
# fm2 port0 = chars 3..10 in order R L D U T S B A ; sol slots 4..11 = U D L R S s B A
order = {4: 3, 5: 2, 6: 1, 7: 0, 8: 4, 9: 5, 10: 6, 11: 7}   # sol slot -> fm2 port0 index
chars = {}
for f, r in zip(frames, refl):
    p0 = f.split('|')[2]
    for slot, idx in order.items():
        if p0[idx] != '.':
            chars.setdefault(slot, set()).add(r[slot])
default = {4: 'U', 5: 'D', 6: 'L', 7: 'R', 8: 'S', 9: 's', 10: 'B', 11: 'A'}
for slot in order:
    if slot in chars and len(chars[slot]) == 1: default[slot] = next(iter(chars[slot]))
    elif slot in chars: print("ambiguous char for slot", slot, chars[slot])
lines = []
for f in frames:
    p0 = f.split('|')[2]
    s = ['|', '.', '.', '|'] + ['.'] * 8 + ['|']
    for slot, idx in order.items():
        if p0[idx] != '.': s[slot] = default[slot]
    lines.append(''.join(s))
open(out, 'w').write('\n'.join(lines) + '\n')
mism = [i for i, (a, b) in enumerate(zip(lines, refl)) if a != b]
print(f"fm2 frames={len(frames)} ref lines={len(refl)} learned chars={default} mismatches in overlap={len(mism)} first={mism[:5]}")
print(f"subtitles={len(subs)}")
open(out + '.subtitles.txt', 'w').write('\n'.join(subs) + '\n')
