# Regions

One repository supports every release of the game. A region is a folder here:

    regions/<id>/region.json   id, serial, boot file name, status, local output folders
    regions/<id>/recomp.toml   recompiler configuration (addresses/options only)
    regions/<id>/README.md

**No forks are needed.** To add a region: copy an existing folder, run `python tools/usa_map.py`-style address mapping (or the analyzer) against the new boot file,
add it to `release/regions.json` and the launcher's `BUILDS` list (`tools/launcher/server.py`).
Shared code (runtime, launcher, tools) is region independent; region-specific hooks live behind the address maps in each region folder.
