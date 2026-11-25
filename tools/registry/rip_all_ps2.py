#!/usr/bin/env python3
"""rip_all_ps2.py <build dir> <budget seconds> -- runs rip_ps2_media.py over every PS2 ISO in turn until the time budget is used; call repeatedly until it prints ALL DONE."""
import os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__)); B = sys.argv[1]; budget = float(sys.argv[2]); T0 = time.time()
D = os.path.expanduser('~/mnt/Desktop/Games Mods and Recomp Library/Games and ISOs/WWE and SmackDown ISOs/PS2')
G = [('WWE SmackDown! vs. Raw 2006 (USA).iso', 'SvR 2006'), ('WWE SmackDown! Here Comes the Pain (USA).iso', 'Here Comes the Pain'),
     ('WWE SmackDown! vs. Raw (Europe, Australia).iso', 'SvR 2004'), ('WWE SmackDown vs. Raw 2007 (USA) (v1.01).iso', 'SvR 2007'),
     ('WWE SmackDown vs. Raw 2008 (USA).iso', 'SvR 2008'), ('WWE SmackDown vs. Raw 2009 (USA) (En,Fr).iso', 'SvR 2009'),
     ('WWE SmackDown vs. Raw 2010 (USA) (En,Fr,Es).iso', 'SvR 2010'), ('WWE SmackDown vs. Raw 2011 (USA) (En,Fr,Es).iso', 'SvR 2011')]
alld = True
for iso, label in G:
    left = budget - (time.time() - T0)
    if left < 20: alld = False; break
    r = subprocess.run([sys.executable, '-u', os.path.join(HERE, 'rip_ps2_media.py'), os.path.join(D, iso), label, B, str(left - 15)], capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or ['?'])[-1]; print(label, '->', last[:160], flush=True)
    if not last.startswith('DONE'): alld = False; break
print('ALL DONE' if alld else 'MORE', flush=True)
