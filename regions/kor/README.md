# Korea (NTSC-K) - SLKA-25318

Status: **planned** (not started).
Boot file: `SLKA_253.18` (supply from your own disc; never committed).

To start this region:
1. `python tools/usa_map.py`-style mapping against the PAL config (copy and adapt for this region) to create `config/svr06_kor_ps2recomp.toml`.
2. Add a `scripts/KOR_BUILD.cmd` modelled on `scripts/USA_BUILD.cmd`.
3. Generate overlay banks with `tools/usa_ovl_all.sh` (adjust CODE_END and the BIN.PAC path).
4. Change `status` to `in-progress` here and in `regions/regions.json`.
