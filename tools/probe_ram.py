#!/usr/bin/env python3
"""probe_ram.py NAME -- RAM dump of the running game (waits for the console to finish) -> analysis/dumps/NAME.bin"""
import sys, os, time, shutil, svrlive
name = sys.argv[1]
svrlive.send(['ramdump'], 30)
src = os.path.join(svrlive.LIVE, 'live_ram.bin'); t0 = time.time()
while time.time() - t0 < 40:
    time.sleep(1)
    if os.path.exists(src) and time.time() - os.path.getmtime(src) < 15 and os.path.getsize(src) == 33554432:
        time.sleep(1.5); break
dst = os.path.join(svrlive.ROOT, 'analysis', 'dumps', name + '.bin'); shutil.copyfile(src, dst); print('saved', dst)
