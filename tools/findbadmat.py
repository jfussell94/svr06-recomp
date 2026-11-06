#!/usr/bin/env python3
"""findbadmat: scan an EE RAM dump for 4x4 matrices (16B-aligned) whose xyz lanes are sane but w lanes of rows 0-2 are huge/garbage."""
import sys, numpy as np
d = np.fromfile(sys.argv[1], dtype='<f4')
q = d.reshape(-1, 4)
a = np.abs(q)
xyz_ok = (a[:, :3] <= 1.5).all(axis=1) & np.isfinite(q).all(axis=1)
w_bad = (a[:, 3] > 1e5) & np.isfinite(q[:, 3])
cand = xyz_ok & w_bad
idx = np.nonzero(cand[:-3] & cand[1:-2] & cand[2:-1])[0]
print(len(idx), [hex(int(i) * 16) for i in idx[:40]])
