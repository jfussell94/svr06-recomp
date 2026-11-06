# Settings menu plan (modelled on Ship of Harkinian / 2Ship2Harkinian) - 2026-10-28

Sources: 2Ship2Harkinian README + 2.0.0 release notes (newreleases.io), Shipwright `SohMenuBar.cpp` and `UIWidgets.cpp` (mirror at code.moparisthebest.com). Ideas only; no code copied (both are GPL/LGPL-licensed, check before reusing).

## How they build the menu
* Dear ImGui menu bar (toggle with F1; 2Ship v2 replaced it with a modern window menu with presets + advanced resolution editor). Top-level: Ship (window/app actions), Settings (Audio, Controller, Graphics, Languages, Accessibility), Enhancements, Cheats, Developer tools.
* Every control is a thin wrapper bound to a named **CVar** (key prefix groups: settings.*, enhancements.*, cheats.*, windows.*): `EnhancementCheckbox`, `EnhancementCombobox` (string array indexed by an int CVar, default index passed in), `EnhancementSliderInt/Float` (min/max, format string, +/- buttons, percent display), `EnhancementRadioButton`, `EnhancementColor`, plus `Tooltip`/`InsertHelpHoverText` ("?" hover) and padded separators.
* Persistence: widget reads CVar with a default, writes on change, config saved on the next tick (JSON file). Dependent options are greyed with a reason tooltip (`DisableComponent` / `ReEnableComponent(reason)`); risky features need a confirmation checkbox first; "needs restart/reload" is written in the tooltip; options gated by backend/platform.
* Dropdown pattern: fixed label array + int CVar, empty entries skipped, optional forced value when disabled.

## Features that fit SvR06
| Group | Items (all via one CVar-style JSON config; env vars become defaults) | Status in our tree |
|---|---|---|
| Graphics | Internal resolution (1x/2x/3x/4x, SSAA from paraLLEl-GS), aspect (4:3 / 16:9 widescreen / stretch), vsync, FPS cap / 60 fps mode, fullscreen/borderless, window scale, texture filtering / anti-blur, deinterlace, overscan crop, FPS counter, renderer = Vulkan (only) | needs P5 presenter; resolution/SSAA are paraLLEl-GS options |
| Audio | master/music/SFX/commentary/crowd volume, SPU2 interpolation (linear/gaussian), music source (native ADX vs IOP), mute, latency | SPU2 model exists; volumes new |
| Controls | keyboard + XInput mapping window, deadzone, rumble on/off, input viewer, controller glyph set | XInput pending (backlog) |
| Gameplay enhancements | skip intro movies/logos, skip loading-screen delay, Unlock All (belts, arenas, wrestlers), match-rule tweaks (count-out length, pin count speed), camera options (distance/style), disable demo timeout | Unlock All and skip-movies in backlog |
| Fixes / Restoration | per-bug toggles for our recomp workarounds (guard Chara update, heap limit, FPU rounding mode SVR06_FPU_TRUNC, VU1 exact vs fast) so a user can flip one when it misbehaves | env vars exist today (SVR06_*) |
| Cheats | god mode, infinite spirit/stamina, no stun, CPU AI difficulty override, save states (F5/F9) behind a confirm checkbox | savestates planned |
| Developer tools | windows: RAM/object viewer (svrdiag), live console, GS frame/VRAM viewer, VU1 profiler, frame-time graph, log viewer, Vulkan/GS diff mode, input viewer | tools exist as scripts; ImGui panel `ps2_debug_panel.cpp` already compiled |
| Presets / profiles | "Original 1:1" (native res, 4:3, no tweaks), "Enhanced" (4x, 16:9, 60 fps), "Performance"; export/import config | new |
| Mods | asset override folder (textures, audio) later; hash-based texture replacement hook at the VRAM upload | P6 idea |
| Accessibility | UI scale, high-contrast HUD text, mute alarm sounds, controller-navigable menus | new |

## Implementation approach (proposal)
1. New `ps2xRuntime/src/lib/ps2_settings.{h,cpp}`: tiny CVar store (typed get/set with default, JSON file `svr06_settings.json`, dirty flag saved once per second). Existing `SVR06_*` env vars override for developers.
2. Menu in ImGui, drawn on the Vulkan presenter (P5). Until then the existing rlImGui debug panel (raylib) can host it; widgets above wrapped the same way (checkbox/combo/slider with CVar key, default, tooltip, disabled reason).
3. Settings that need a restart are marked; others apply live (volumes, FPS cap, filters).
4. Menu bar toggled with F1; F11 fullscreen; F5/F9 savestate once implemented.

## Implementation status (13:42)
Working: fullscreen, texture filter, FPS counter, master volume, mute, presets/reset, menu bar. All other entries exist in the menu labelled "(in development)" and only store a value.
