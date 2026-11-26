# paraLLEl-GS local changes (LGPL-3.0+)

`scripts/usa_build.ps1` clones upstream https://github.com/Arntzen-Software/parallel-gs at commit
`cc6184af7e0c03da603045ca371ffa5dae9b0655` (with its submodules, Granite at `16e7395f`) into `PS2Recomp_usa/ps2xRuntime/third_party/parallel-gs`
and then copies the files in this folder over it. These are this project's modifications to paraLLEl-GS / Granite, published under the same LGPL-3.0+ licence:

- `gs/gs_interface.cpp`, `gs/gs_interface.hpp` - runtime hooks (texture replacement, timing, host integration)
- `gs/shaders/*` - regenerated shader headers and the modified ubershader
- `Granite/vulkan/context.cpp` - GPU selection (`SVR06_GPU`)
