#!/usr/bin/env python3
"""tokens -> .sol : tok2sol.py <tokens.txt> <out.sol> [pad=10]"""
import sys
toks=open(sys.argv[1]).read().split(); pad=int(sys.argv[3]) if len(sys.argv)>3 else 10
o=[]
for t in toks+['-']*pad:
    s=list('|..|........|')
    for c in t:
        if c=='U': s[4]='U'
        if c=='D': s[5]='D'
        if c=='L': s[6]='L'
        if c=='R': s[7]='R'
        if c=='B': s[10]='B'
        if c=='A': s[11]='A'
    o.append(''.join(s))
open(sys.argv[2],'w').write('\n'.join(o)+'\n')
