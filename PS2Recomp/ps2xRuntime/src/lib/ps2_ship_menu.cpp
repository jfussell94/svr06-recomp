// SvR06 in-game settings menu - layout/look follows the Ship of Harkinian (libultraship) menu:
// top tab row, search box, red/red/grey system buttons, white rule, centered sidebar, two content columns,
// blue widgets with labels above sliders/combos, tooltips, greyed items with a reason.
// Pure raylib (no ImGui). Montserrat Regular (SIL OFL 1.1) is embedded.
#include "raylib.h"
#include "ps2_settings.h"
#include "ps2_cheat_tables.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#ifdef _WIN32
extern "C" __declspec(dllimport) const char *__stdcall GetCommandLineA(void);
#endif

#include "ps2_ship_font.inc"

std::atomic<bool> g_svr06MenuOpen{false};
namespace svr06input
{
struct PadBind { const char *name; uint16_t mask; int defBtn; };
extern const PadBind kPadBinds[16];
int defaultKey(int i, int alt);
}
extern std::atomic<uint64_t> g_svr06PadView[2];
extern std::atomic<int> g_svr06Flags;

namespace
{
enum Type { T_HEADER, T_CHECK, T_SLIDER, T_COMBO, T_BUTTON, T_TEXT, T_FLAG, T_BIND, T_VIEW };
struct Item
{
    int tab, side, col;
    Type t;
    const char *label;
    const char *key;
    double def, lo, hi;
    const char *unit;           // "%" shows percentage, otherwise printf suffix
    const char *const *opts;
    int nopts;
    const char *tip;
    const char *disabled;       // reason, nullptr = enabled
    bool reload;
    int action;
};

const char *kTabNames[4] = {"Settings", "Enhancements", "Cheats", "Dev Tools"};
const char *kSide0[] = {"General", "Audio", "Graphics", "Controls", "Game", "Accessibility", "Presets"};
const char *kSide1[] = {"Gameplay", "Interface", "Music"};
const char *kSide2[] = {"Match", "Locker Room", "Shop Unlocks", "GM & Season"};
const char *kSide3[] = {"Debug Flags", "Info"};
const char *const *kSides[4] = {kSide0, kSide1, kSide2, kSide3};
const int kSideN[4] = {7, 3, 4, 2};

const char *kThemeNames[] = {"Blue", "Indigo", "Purple", "Red", "Orange", "Green", "Gray"};
const Color kThemeCol[] = {{0, 61, 204, 255}, {63, 51, 190, 255}, {120, 40, 170, 255}, {190, 25, 25, 255}, {210, 105, 10, 255}, {20, 130, 60, 255}, {90, 90, 100, 255}};
const char *kFilt[] = {"Nearest (sharp)", "Bilinear (smooth)"};
const char *kAsp[] = {"4:3 (original)", "16:9", "21:9", "Stretch"};
const char *kScale[] = {"1x Native (fastest)", "1x + Anti-aliasing (SSAA 4x)", "2x (1280x896)", "2x + Anti-aliasing (SSAA 8x)"};
const char *kLayout[] = {"Docked", "Full"};
const char *kColorBlind[] = {"Off", "Protanopia (red-weak)", "Deuteranopia (green-weak)", "Tritanopia (blue-weak)"};
const char *kMode[] = {"Normal (VU1 speed-ups)", "Safe (reference path, slower)"};
const char *kIcons[] = {"Original (PS2)", "Xbox One", "Xbox 360", "PS3", "PS4", "PS5", "Switch Pro", "Switch Pro 2", "Custom pack"};
const char *kFinOpts[] = {"Off", "1", "2", "Unlimited"};
const char *kRend[] = {"Vulkan (paraLLEl-GS)", "Software (reference)"};
const char *kCam[] = {"Original", "Wide", "Close"};
const char *kAi[] = {"Original", "Easy", "Hard"};
const char *kInterp[] = {"Gaussian (original)", "Linear"};

const char *DEV = "Not implemented yet (in development).";
const char *kDev[] = {"Auto", "Gamepad 1", "Gamepad 2", "Gamepad 3", "Gamepad 4", "None"};
const char *kPadBtnName[18] = {"(none)", "D-Pad Up", "D-Pad Right", "D-Pad Down", "D-Pad Left", "Y / Triangle", "B / Circle", "A / Cross",
                                "X / Square", "LB / L1", "LT / L2", "RB / R1", "RT / R2", "Back / Select", "Guide", "Start", "Left Stick Click", "Right Stick Click"};
const char *keyName(int k)
{
    static char buf[24];
    if (k <= 0) return "(none)";
    if (k >= 'A' && k <= 'Z') { buf[0] = (char)k; buf[1] = 0; return buf; }
    if (k >= '0' && k <= '9') { buf[0] = (char)k; buf[1] = 0; return buf; }
    switch (k)
    {
    case KEY_SPACE: return "Space"; case KEY_ESCAPE: return "Esc"; case KEY_ENTER: return "Enter"; case KEY_TAB: return "Tab"; case KEY_BACKSPACE: return "Backspace";
    case KEY_UP: return "Up Arrow"; case KEY_DOWN: return "Down Arrow"; case KEY_LEFT: return "Left Arrow"; case KEY_RIGHT: return "Right Arrow";
    case KEY_LEFT_SHIFT: return "Left Shift"; case KEY_RIGHT_SHIFT: return "Right Shift"; case KEY_LEFT_CONTROL: return "Left Ctrl"; case KEY_RIGHT_CONTROL: return "Right Ctrl";
    case KEY_LEFT_ALT: return "Left Alt"; case KEY_RIGHT_ALT: return "Right Alt"; case KEY_INSERT: return "Insert"; case KEY_DELETE: return "Delete";
    case KEY_HOME: return "Home"; case KEY_END: return "End"; case KEY_PAGE_UP: return "Page Up"; case KEY_PAGE_DOWN: return "Page Down";
    case KEY_COMMA: return ","; case KEY_PERIOD: return "."; case KEY_SLASH: return "/"; case KEY_SEMICOLON: return ";"; case KEY_APOSTROPHE: return "'";
    case KEY_LEFT_BRACKET: return "["; case KEY_RIGHT_BRACKET: return "]"; case KEY_BACKSLASH: return "\\"; case KEY_MINUS: return "-"; case KEY_EQUAL: return "="; case KEY_GRAVE: return "`";
    case KEY_KP_ENTER: return "Numpad Enter"; case KEY_KP_ADD: return "Numpad +"; case KEY_KP_SUBTRACT: return "Numpad -"; case KEY_KP_MULTIPLY: return "Numpad *"; case KEY_KP_DIVIDE: return "Numpad /"; case KEY_KP_DECIMAL: return "Numpad .";
    default: break;
    }
    if (k >= KEY_KP_0 && k <= KEY_KP_9) { std::snprintf(buf, sizeof buf, "Numpad %d", k - KEY_KP_0); return buf; }
    if (k >= KEY_F1 && k <= KEY_F12) { std::snprintf(buf, sizeof buf, "F%d", k - KEY_F1 + 1); return buf; }
    std::snprintf(buf, sizeof buf, "Key %d", k); return buf;
}
const char *kPs2Label[16] = {"Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Select", "Start"};
int g_bindCapture = -1;   // item index waiting for a gamepad button
double g_bindCaptureT = 0;
std::deque<std::string> g_keyStore; // stable storage for generated setting keys
const char *K(const char *fmt, int a, const char *b = "")
{
    char buf[96]; std::snprintf(buf, sizeof buf, fmt, a, b); g_keyStore.emplace_back(buf); return g_keyStore.back().c_str();
}
const char *K2(const char *fmt, const char *a)
{
    char buf[96]; std::snprintf(buf, sizeof buf, fmt, a); g_keyStore.emplace_back(buf); return g_keyStore.back().c_str();
}

// actions
enum { A_NONE, A_FULLSCREEN, A_RESET_ALL, A_OPEN_FOLDER, A_PRESET_ORIG, A_PRESET_SMOOTH, A_PRESET_ENH, A_RESET_IMG };

std::vector<Item> g_items;
void I(int tab, int side, int col, Type t, const char *label, const char *key = nullptr, double def = 0, double lo = 0, double hi = 100,
       const char *unit = nullptr, const char *const *opts = nullptr, int nopts = 0, const char *tip = nullptr, const char *dis = nullptr,
       bool reload = false, int action = 0)
{
    g_items.push_back(Item{tab, side, col, t, label, key, def, lo, hi, unit, opts, nopts, tip, dis, reload, action});
}

void buildItems()
{
    if (!g_items.empty()) return;
    // ---- Settings / General
    I(0, 0, 0, T_HEADER, "Menu Settings");
    I(0, 0, 0, T_COMBO, "Menu Theme", "menu.theme", 0, 0, 0, nullptr, kThemeNames, 7, "Changes the accent color of this menu.");
    I(0, 0, 0, T_SLIDER, "Menu Background Opacity", "menu.opacity", 88, 0, 100, "%", nullptr, 0, "Sets the opacity of the menu background.");
    I(0, 0, 0, T_COMBO, "Menu Layout", "menu.layout", 0, 0, 0, nullptr, kLayout, 2, "Docked keeps the game visible on the left while you change things. Full is the Ship of Harkinian layout.");
    I(0, 0, 0, T_CHECK, "Fade Menu While Dragging a Slider", "menu.autoPeek", 1, 0, 1, nullptr, nullptr, 0, "Fades the menu out while you drag a slider so you can see the effect on the picture. You can also hold the eye button in the top bar.");
    I(0, 0, 0, T_SLIDER, "Menu Scale", "menu.scale", 100, 70, 200, "%", nullptr, 0, "Scales the whole menu. Raise it for reading from a distance.");
    I(0, 5, 0, T_HEADER, "Vision");
    I(0, 5, 0, T_COMBO, "Color Blind Mode", "access.colorblind", 0, 0, 0, nullptr, kColorBlind, 4, "Corrects the game picture so colors that look alike become easier to tell apart. Pick the type that matches your vision.");
    I(0, 5, 0, T_SLIDER, "Color Blind Strength", "access.colorblindStrength", 100, 0, 100, "%", nullptr, 0, "How strongly the correction is applied.");
    I(0, 5, 0, T_HEADER, "Menu Readability");
    I(0, 5, 0, T_CHECK, "High Contrast Menu", "menu.highContrast", 0, 0, 1, nullptr, nullptr, 0, "Brighter text and a fully opaque menu background for maximum readability.");
    I(0, 5, 0, T_CHECK, "Bold Menu Text", "menu.boldText", 0, 0, 1, nullptr, nullptr, 0, "Thickens menu text so it stays readable at a distance.");
    I(0, 5, 0, T_CHECK, "Reduce Menu Motion", "menu.reduceMotion", 0, 0, 1, nullptr, nullptr, 0, "Turns off menu fade and slide animations.");
    I(0, 5, 0, T_HEADER, "Game Input");
    I(0, 5, 0, T_CHECK, "Mash Assist (Player 1)", "input.p1.mashAssist", 0, 0, 1, nullptr, nullptr, 0, "Hold Cross (X) to rapid-fire it for pin and submission escapes instead of mashing. Pulse rate set below.");
    I(0, 5, 0, T_CHECK, "Mash Assist (Player 2)", "input.p2.mashAssist", 0, 0, 1, nullptr, nullptr, 0, "Same as above for Player 2.");
    I(0, 5, 0, T_SLIDER, "Mash Assist Speed", "input.mashHz", 12, 4, 20, " /s", nullptr, 0, "Presses per second while Mash Assist is active.");
    I(0, 5, 0, T_CHECK, "Sticky Shoulder Buttons (P1)", "input.p1.sticky", 0, 0, 1, nullptr, nullptr, 0, "L1/R1/L2/R2 toggle on press instead of needing to be held. Press again to release.");
    I(0, 0, 0, T_HEADER, "General Settings");
    I(0, 0, 0, T_CHECK, "Show FPS Counter", "graphics.showFps", 1, 0, 1, nullptr, nullptr, 0, "Shows the frame counter in the top left corner (F3).");
    I(0, 0, 0, T_BUTTON, "Open App Files Folder", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Opens the folder that holds svr06_settings.json and the logs.", nullptr, false, A_OPEN_FOLDER);
    I(0, 0, 0, T_BUTTON, "Reset All Settings", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Restores every setting in this menu to its default.", nullptr, false, A_RESET_ALL);
    I(0, 0, 1, T_HEADER, "About");
    I(0, 0, 1, T_TEXT, "WWE SmackDown! vs. Raw 2006");
    I(0, 0, 1, T_TEXT, "Native PC static recompilation (PS2Recomp), PAL SLES-53676.");
    I(0, 0, 1, T_TEXT, "Rendering: Vulkan (paraLLEl-GS) or Software, see Graphics > Renderer API.");
    I(0, 0, 1, T_TEXT, "Menu style after Ship of Harkinian (libultraship, MIT).");
    I(0, 0, 1, T_TEXT, "F1 opens/closes this menu. F11 toggles fullscreen. F3 toggles the FPS counter.");
    // ---- Settings / Audio
    I(0, 1, 0, T_HEADER, "Audio Settings");
    I(0, 1, 0, T_SLIDER, "Master Volume", "audio.master", 100, 0, 100, "%", nullptr, 0, "Adjust the overall volume.");
    I(0, 1, 0, T_CHECK, "Mute", "audio.mute", 0, 0, 1, nullptr, nullptr, 0, "Mutes all game audio.");
    I(0, 1, 1, T_HEADER, "Mixer");
    I(0, 1, 1, T_SLIDER, "Music Volume", "audio.music", 100, 0, 100, "%", nullptr, 0, nullptr, DEV);
    I(0, 1, 1, T_SLIDER, "Effects Volume", "audio.sfx", 100, 0, 100, "%", nullptr, 0, nullptr, DEV);
    I(0, 1, 1, T_SLIDER, "Commentary Volume", "audio.commentary", 100, 0, 100, "%", nullptr, 0, nullptr, DEV);
    I(0, 1, 1, T_SLIDER, "Crowd Volume", "audio.crowd", 100, 0, 100, "%", nullptr, 0, nullptr, DEV);
    I(0, 1, 1, T_COMBO, "SPU2 Interpolation", "audio.interp", 0, 0, 0, nullptr, kInterp, 2, nullptr, DEV, true);
    // ---- Settings / Graphics
    I(0, 2, 0, T_HEADER, "Graphics Options");
    I(0, 2, 0, T_BUTTON, "Toggle Fullscreen", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Toggles fullscreen (F11).", nullptr, false, A_FULLSCREEN);
    I(0, 2, 0, T_COMBO, "Internal Resolution", "graphics.internalRes", 0, 0, 0, nullptr, kScale, 4, "Renders the 3D picture with more samples on the GPU. Native is the original look and the fastest. Restart the game after changing it.", nullptr, true);
    I(0, 2, 0, T_SLIDER, "Anti-aliasing (MSAA)", "graphics.msaa", 1, 1, 8, "x", nullptr, 0, nullptr, DEV);
    I(0, 2, 0, T_SLIDER, "Current FPS", "graphics.fpsCap", 60, 20, 240, " fps", nullptr, 0, "Frame rate limit. The game runs at its original 50/60 Hz logic rate.", "Frame pacing work is not finished (in development).");
    I(0, 2, 0, T_CHECK, "Enable Vsync", "graphics.vsync", 1, 0, 1, nullptr, nullptr, 0, "Synchronises presentation with the monitor refresh.");
    I(0, 2, 0, T_SLIDER, "Frame Queue (buffering)", "graphics.frameQueue", 1, 1, 16, "x", nullptr, 0, "Frames the GPU may work ahead of the picture you see. 1 = off (default, no added input delay; full-screen movies queue one frame automatically). Each extra frame adds about 16 ms of input delay.");
    I(0, 2, 0, T_COMBO, "Renderer API", "graphics.renderer", 0, 0, 0, nullptr, kRend, 2, "Vulkan (paraLLEl-GS) is the player renderer. Software is the slow CPU reference renderer for testing; restart the game after changing it.", nullptr, true);
    I(0, 2, 0, T_COMBO, "Texture Filter", "graphics.filter", 0, 0, 0, nullptr, kFilt, 2, "Nearest keeps the PS2 look; Bilinear smooths the final image.");
    I(0, 2, 1, T_HEADER, "Advanced Graphics Options");
    I(0, 2, 1, T_COMBO, "Aspect Ratio", "graphics.aspect", 0, 0, 0, nullptr, kAsp, 4, "The picture is rendered at 4:3; wider modes stretch it (widescreen HUD fixes are in development).");
    I(0, 2, 1, T_CHECK, "Integer Scaling", "graphics.integer", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(0, 2, 1, T_CHECK, "Deinterlace / Anti-blur", "graphics.antiBlur", 1, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(0, 2, 1, T_HEADER, "Image Adjustments");
    I(0, 2, 1, T_SLIDER, "Brightness", "image.brightness", 0, -100, 100, "%", nullptr, 0, "Adds or removes light from the whole picture.");
    I(0, 2, 1, T_SLIDER, "Contrast", "image.contrast", 0, -100, 100, "%", nullptr, 0, "Spreads or compresses the difference between dark and bright.");
    I(0, 2, 1, T_SLIDER, "Saturation", "image.saturation", 100, 0, 200, "%", nullptr, 0, "0% is black and white, 100% is the original colors.");
    I(0, 2, 1, T_SLIDER, "Vibrance", "image.vibrance", 0, -100, 100, "%", nullptr, 0, "Boosts muted colors more than already vivid ones.");
    I(0, 2, 1, T_SLIDER, "Gamma", "image.gamma", 100, 50, 200, "%", nullptr, 0, "Brightens or darkens the midtones.");
    I(0, 2, 1, T_SLIDER, "Black Level", "image.black", 0, 0, 50, "%", nullptr, 0, "Lifts the darkest values to pure black.");
    I(0, 2, 1, T_SLIDER, "Color Temperature", "image.temp", 0, -100, 100, "%", nullptr, 0, "Negative is cooler (blue), positive is warmer (orange).");
    I(0, 2, 1, T_SLIDER, "Hue Shift", "image.hue", 0, -180, 180, " deg", nullptr, 0, "Rotates every color around the color wheel.");
    I(0, 2, 1, T_SLIDER, "Sharpness", "image.sharp", 0, 0, 100, "%", nullptr, 0, "Sharpens edges (unsharp mask).");
    I(0, 2, 1, T_BUTTON, "Reset Image Adjustments", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Returns every image adjustment to neutral.", nullptr, false, A_RESET_IMG);
    // ---- Settings / Controls (per player, PCSX2-style mapping; port 1 = Player 1, port 2 = Player 2)
    for (int pl = 0; pl < 2; ++pl)
    {
        I(0, 3, pl, T_HEADER, pl == 0 ? "Player 1 (port 1)" : "Player 2 (port 2)");
        I(0, 3, pl, T_VIEW, "Input Viewer", nullptr, 0, 0, 0, nullptr, nullptr, pl, "Shows what the game sees from this player's pad right now (after mapping and accessibility options). Lit = pressed.");
        I(0, 3, pl, T_COMBO, "Controller", K("input.p%d.device", pl + 1), 0, 0, 0, nullptr, kDev, 6,
          "Auto: Player 1 takes the first connected gamepad, Player 2 the second. Pick a number to pin a pad; None disconnects this player's pad.");
        I(0, 3, pl, T_CHECK, "Keyboard", K("input.p%d.keyboard", pl + 1), pl == 0 ? 1 : 0, 0, 1, nullptr, nullptr, 0,
          "Keyboard also drives this player: arrows/WASD, X/Space = Cross, C = Circle, Z = Square, V = Triangle, Q/E = L1/R1, Shift = L2/R2, Enter = Start, Tab = Select.");
        I(0, 3, pl, T_CHECK, "Left Stick Moves D-Pad", K("input.p%d.stickdpad", pl + 1), 1, 0, 1, nullptr, nullptr, 0, "The left stick also presses the D-pad directions (the game moves wrestlers with both).");
        for (int i = 0; i < 16; ++i)
            I(0, 3, pl, T_BIND, kPs2Label[i], K("input.p%d.btn.%s", pl + 1, svr06input::kPadBinds[i].name), svr06input::kPadBinds[i].defBtn, 0, 0, nullptr, nullptr, 0,
              "Click, then press the gamepad button to use. Right-click resets to the default. Esc cancels.");
        I(0, 3, pl, T_HEADER, pl == 0 ? "Player 1 keyboard keys" : "Player 2 keyboard keys");
        for (int i = 0; i < 16; ++i)
        {
            I(0, 3, pl, T_BIND, K2("%s (key)", kPs2Label[i]), K("input.p%d.key.%s", pl + 1, svr06input::kPadBinds[i].name), svr06input::defaultKey(i, 0), 0, 0, nullptr, nullptr, 1,
              "Click, then press the keyboard key to use. Right-click resets to the default, Backspace clears it. Esc cancels. Only used when Keyboard is ticked for this player.");
            I(0, 3, pl, T_BIND, K2("%s (alt key)", kPs2Label[i]), K("input.p%d.key2.%s", pl + 1, svr06input::kPadBinds[i].name), svr06input::defaultKey(i, 1), 0, 0, nullptr, nullptr, 1,
              "A second key for the same button. Right-click resets to the default, Backspace clears it.");
        }
    }
    I(0, 3, 0, T_SLIDER, "Stick Deadzone", "controls.deadzone", 15, 0, 50, "%", nullptr, 0, "Stick movement inside this range counts as centred (both players).");
    I(0, 3, 1, T_CHECK, "Rumble", "controls.rumble", 1, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    // ---- Settings / Game (what the launcher's Play / Visuals pages offer; stored in svr06_settings.json, read at startup)
    I(0, 4, 0, T_HEADER, "Performance");
    I(0, 4, 0, T_COMBO, "Emulation Mode", "game.mode", 0, 0, 0, nullptr, kMode, 2, "Normal uses the VU1 speed-ups. Safe turns them off: slower, but it is the reference path if something looks wrong.", nullptr, true);
    I(0, 4, 0, T_CHECK, "Diagnostic Probes", "game.probes", 1, 0, 1, nullptr, nullptr, 0, "Writes diagnostic probe lines to the log for bug reports. Turning them off is a little faster.", nullptr, true);
    I(0, 4, 0, T_HEADER, "Appearance");
    I(0, 4, 0, T_COMBO, "Controller Button Icons", "game.iconSet", 0, 0, 0, nullptr, kIcons, 9, "Replaces the Cross / Square / Triangle / Circle, shoulder and D-pad prompts in menus. PAL version only.", nullptr, true);
    I(0, 4, 0, T_CHECK, "HD Texture Pack Detection", "game.texpack", 0, 0, 1, nullptr, nullptr, 0, "Hashes the game's textures like PCSX2 and logs which ones the HD pack covers (stage 1 of 3). PAL version only.", nullptr, true);
    I(0, 4, 1, T_HEADER, "Extras");
    I(0, 4, 1, T_CHECK, "Skip Intro Movies", "game.skipMovies", 0, 0, 1, nullptr, nullptr, 0, "Skips the start-up logos and intro movies.", nullptr, true);
    I(0, 4, 1, T_CHECK, "PSP Arcade Extras", "game.arcade", 0, 0, 1, nullptr, nullptr, 0, "Game Show, Eugene and Hold'em from the SmackDown! vs. Raw 2006 PSP game. The PSP game must be linked in the launcher once.", nullptr, true);
    // ---- Settings / Presets
    I(0, 6, 0, T_HEADER, "Presets");
    I(0, 6, 0, T_BUTTON, "Apply: Original 1:1", nullptr, 0, 0, 0, nullptr, nullptr, 0, "4:3, nearest filter, neutral image, vsync on.", nullptr, false, A_PRESET_ORIG);
    I(0, 6, 0, T_BUTTON, "Apply: Smooth", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Bilinear filter, neutral image.", nullptr, false, A_PRESET_SMOOTH);
    I(0, 6, 0, T_BUTTON, "Apply: Enhanced", nullptr, 0, 0, 0, nullptr, nullptr, 0, "16:9, bilinear filter, slight contrast, saturation and sharpness.", nullptr, false, A_PRESET_ENH);
    // ---- Enhancements
    I(1, 0, 0, T_HEADER, "Gameplay");
    I(1, 0, 0, T_CHECK, "Disable Demo Timeout", "game.noDemo", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(1, 0, 0, T_CHECK, "Skip Loading-Screen Delay", "game.fastLoad", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(1, 0, 1, T_HEADER, "Matches");
    I(1, 0, 1, T_COMBO, "Camera Style", "game.camera", 0, 0, 0, nullptr, kCam, 3, nullptr, DEV);
    I(1, 0, 1, T_SLIDER, "Count-out Length", "game.countout", 10, 5, 30, " s", nullptr, 0, nullptr, DEV);
    I(1, 1, 0, T_HEADER, "Interface");
    I(1, 1, 0, T_CHECK, "Widescreen HUD", "game.wideHud", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(1, 2, 0, T_HEADER, "Custom Music");
    I(1, 2, 0, T_CHECK, "Use replacement tracks", "music.replace", 0, 0, 1, nullptr, nullptr, 0, "Plays your own file instead of a stock song (e.g. its full-length version). Put it in the \"Custom Music\\Replacements\" folder named like BGM2_012.ogg; the name of the song playing right now is in Replacements\\_last_played.txt. Off = original songs (1:1).");
    I(1, 2, 0, T_CHECK, "Allows extra music slots", "music.extraSlots", 0, 0, 1, nullptr, nullptr, 0, "Adds the songs in \"Custom Music\\Entrances\" (and its game sub-folders, e.g. Entrances\\SVR05) to the Create-an-Entrance music list, sorted A-Z with \"none\" last. The list refreshes every time it is opened. Off = original 1:1 list.", "Up to 165 extra songs (ISS-143). Each song keeps its slot in Entrances\\_slots.txt so saved entrances keep their song.");
    I(1, 2, 0, T_CHECK, "Tag game of origin in the music list", "music.tagStock", 1, 0, 1, nullptr, nullptr, 0, "With extra music slots on, every song name shows the game it comes from: \"Edge '06\" for this game's songs, \"Edge '05\" for one from an SVR05 folder. Names are shortened to fit the row.");
    I(1, 2, 0, T_CHECK, "Now-playing popup", "music.popup", 1, 0, 1, nullptr, nullptr, 0, "Shows \"Artist - Title (Game)\" for 6 s when a custom or replacement song starts (from the song's .json sidecar or its own tags).");
    // ---- Cheats: generated from ps2_cheat_tables.h (same ids as the launcher); the game applies them itself
    {
        using namespace svr06cheat;
        auto S = [](const std::string &x) { g_keyStore.emplace_back(x); return g_keyStore.back().c_str(); };
        auto note = [](const std::string &id) -> const char * {
            if (id == "exh_open_titles") return " Applies live: reopen the match setup screen.";
            if (id.rfind("gm_", 0) == 0) return " Applies live: re-enter the GM screen.";
            if (id == "finisher_start") return " Applies live: takes effect when the next match starts.";
            if (id == "god_mode" || id == "auto_reversal" || id == "finisher_always") return " Applies live during a match.";
            if (id == "unlock_npc") return " Applies live: leave and re-enter character select.";
            return " Applies live: re-enter the shop screen to see it.";
        };
        std::string lastHdr[4];
        for (int c = 0; c < kCheatCount; ++c)
        {
            const CheatDef &d = kCheats[c];
            const bool todo = std::strcmp(d.status, "todo") == 0;
            const char *dis = todo ? DEV : nullptr;
            const std::string grp = d.grp;
            const int side = d.kind == 2 ? 2 : ((grp == "Exhibition" || grp == "Match") ? 0 : (grp == "Locker Room" ? 1 : 3));
            const char *tip = S(std::string(d.desc) + (d.desc[0] ? "" : "") + note(d.id));
            if (d.kind != 2 && lastHdr[side] != grp) { lastHdr[side] = grp; I(2, side, 0, T_HEADER, S(grp)); }
            if (d.kind == 2)
            {
                I(2, side, 0, T_HEADER, S(d.name));
                I(2, side, 0, T_BUTTON, "Select All", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Ticks every item in this list.", nullptr, false, 100 + c);
                I(2, side, 0, T_BUTTON, "Clear", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Unticks every item in this list.", nullptr, false, 200 + c);
                for (int i = 0; i < d.count; ++i)
                    I(2, side, 0, T_CHECK, kShop[d.first + i].name, S(std::string("cheat.") + d.id + "." + std::to_string(kShop[d.first + i].n)), 0, 0, 1, nullptr, nullptr, 0, tip, dis);
                continue;
            }
            for (int pl = 0; pl < (d.per ? 2 : 1); ++pl)
            {
                const std::string label = d.per ? std::string(d.name) + (pl ? " - Player 2" : " - Player 1") : std::string(d.name);
                const std::string key = std::string("cheat.") + d.id + (d.per ? (pl ? ".p2" : ".p1") : "");
                if (d.kind == 1) I(2, side, 0, T_COMBO, S(label), S(key), 0, 0, 0, nullptr, kFinOpts, 4, tip, dis);
                else I(2, side, 0, T_CHECK, S(label), S(key), 0, 0, 1, nullptr, nullptr, 0, tip, dis);
            }
        }
        I(2, 3, 1, T_HEADER, "Not in the launcher");
        I(2, 3, 1, T_CHECK, "Infinite Spirit / Stamina", "cheat.spirit", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
        I(2, 3, 1, T_CHECK, "No Stun", "cheat.noStun", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
        I(2, 3, 1, T_COMBO, "CPU Difficulty Override", "cheat.ai", 0, 0, 0, nullptr, kAi, 3, nullptr, DEV);
    }
    // ---- Dev Tools
    I(3, 0, 0, T_HEADER, "Debug Flags");
    I(3, 0, 0, T_FLAG, "VU1 MSCAL log", nullptr, 2, 0, 0, nullptr, nullptr, 0, "Writes analysis/live/vu1log.txt");
    I(3, 0, 0, T_FLAG, "Log big DMA packets", nullptr, 3);
    I(3, 0, 0, T_FLAG, "VU0 call dump", nullptr, 4);
    I(3, 0, 0, T_FLAG, "Dump request (one shot)", nullptr, 5);
    I(3, 0, 1, T_HEADER, "More Flags");
    I(3, 0, 1, T_FLAG, "Texture dump (ppm)", nullptr, 7);
    I(3, 0, 1, T_FLAG, "No parallel sprites", nullptr, 8);
    I(3, 0, 1, T_FLAG, "VU1 exact interpreter (slow)", nullptr, 9, 0, 0, nullptr, nullptr, 0, "Runs VU1 microcode through the exact interpreter.");
    I(3, 0, 1, T_FLAG, "VU1 fast-vs-exact A/B check", nullptr, 10);
    I(3, 1, 0, T_HEADER, "Info");
    I(3, 1, 0, T_TEXT, "These switches are for diagnosing the recompiler.");
    I(3, 1, 0, T_TEXT, "Settings are stored in svr06_settings.json.");
}

// ---------------- state
Font g_font{};
bool g_fontOk = false;
int g_tab = 0, g_side[4] = {0, 0, 0, 0};
float g_scroll[4][8] = {}, g_scrollT[4][8] = {};
bool g_searchFocus = false;
std::string g_search;
int g_openCombo = -1;       // item index of the open dropdown
int g_popSel = 0;           // keyboard selection inside the open dropdown
Rectangle g_comboCtrl{};
int g_dragSlider = -1;
int g_confirm = 0;          // action awaiting confirmation (1000+ = system buttons)
double g_confirmT = 0;
std::string g_tipText;
bool g_tipDisabled = false;
bool g_wasHidden = false;
float g_vis = 0, g_peek = 0, g_A = 1;   // open animation, "peek at the game" fade, current alpha multiplier
int g_focus = -1;                       // keyboard / gamepad focus (item index)
bool g_kbNav = false, g_ensure = false;
std::vector<int> g_nav;                 // focusable items of the last drawn page
std::vector<float> g_hov, g_knob;       // per-item animation state
std::map<std::string, double> g_startVals;
float g_cTop = 0, g_cH = 0;             // content area (screen px)
float *g_scrT = nullptr;                // scroll target of the page being drawn
float g_dt = 0.016f;

struct Pal { Color main, light, dark, search, on; } g_pal;
const Color kBg{13, 14, 19, 255}, kCard{26, 28, 36, 255}, kOff{66, 70, 84, 255}, kAmber{255, 196, 92, 255};

Color lerpC(Color a, Color b, float t) { return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255}; }
Color mulC(Color a, float f) { return Color{(unsigned char)std::min(255.f, a.r * f), (unsigned char)std::min(255.f, a.g * f), (unsigned char)std::min(255.f, a.b * f), 255}; }
Color withA(Color c, float a) { c.a = (unsigned char)std::max(0.f, std::min(255.f, a * 255.f)); return c; }
Color fa(Color c) { c.a = (unsigned char)(c.a * g_A); return c; }
bool g_reduceMotion = false, g_highContrast = false, g_boldText = false;
float approach(float cur, float tgt, float rate) { if (g_reduceMotion) return tgt; return cur + (tgt - cur) * (1.0f - std::exp(-rate * g_dt)); }

float g_S = 1;
float L(float v) { return v * g_S; }

void text(const char *s, float x, float y, float size, Color c)
{
    if (g_highContrast) { c.r = (unsigned char)std::min(255, c.r + (255 - c.r) * 3 / 4); c.g = (unsigned char)std::min(255, c.g + (255 - c.g) * 3 / 4); c.b = (unsigned char)std::min(255, c.b + (255 - c.b) * 3 / 4); c.a = 255; }
    if (g_boldText && g_fontOk) DrawTextEx(g_font, s, Vector2{x + size * g_S * 0.035f, y}, size * g_S, 0, fa(c));
    if (g_fontOk) DrawTextEx(g_font, s, Vector2{x, y}, size * g_S, 0, fa(c));
    else DrawText(s, (int)x, (int)y, (int)(size * g_S), fa(c));
}
void textB(const char *s, float x, float y, float size, Color c)
{
    text(s, x, y, size, c); text(s, x + L(0.7f), y, size, c);
}
Vector2 measure(const char *s, float size)
{
    if (g_fontOk) return MeasureTextEx(g_font, s, size * g_S, 0);
    return Vector2{(float)MeasureText(s, (int)(size * g_S)), size * g_S};
}
void textC(const char *s, Rectangle r, float size, Color c)
{
    const Vector2 m = measure(s, size);
    text(s, r.x + (r.width - m.x) * 0.5f, r.y + (r.height - m.y) * 0.5f, size, c);
}
void textR(const char *s, float right, float y, float size, Color c) { text(s, right - measure(s, size).x, y, size, c); }
float textMid(float y, float h, float size) { return y + (h - measure("Ag", size).y) * 0.5f; }
void rrect(Rectangle r, float radius, Color c)
{
    const float rr = std::min(r.width, r.height) * 0.5f;
    DrawRectangleRounded(r, std::min(1.0f, L(radius) / std::max(1.0f, rr)), 8, fa(c));
}
void rrectL(Rectangle r, float radius, float th, Color c)
{
    const float rr = std::min(r.width, r.height) * 0.5f;
    DrawRectangleRoundedLinesEx(r, std::min(1.0f, L(radius) / std::max(1.0f, rr)), 8, std::max(1.0f, L(th)), fa(c));
}
void fillR(Rectangle r, Color c) { DrawRectangleRec(r, fa(c)); }
void circle(float x, float y, float rad, Color c) { DrawCircleV(Vector2{x, y}, rad, fa(c)); }
void lineEx(Vector2 a, Vector2 b, float th, Color c) { DrawLineEx(a, b, th, fa(c)); }
bool hit(Rectangle r, Vector2 p) { return CheckCollisionPointRec(p, r); }

std::vector<std::string> wrapText(const std::string &s, float size, float maxW)
{
    std::vector<std::string> lines; std::string cur, word;
    auto push = [&]() {
        if (word.empty()) return;
        const std::string t2 = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && measure(t2.c_str(), size).x > maxW) { lines.push_back(cur); cur = word; } else cur = t2;
        word.clear();
    };
    for (char ch : s) { if (ch == ' ') push(); else word += ch; }
    push();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

void applyPalette()
{
    int th = ps2settings::getInt("menu.theme", 0);
    if (th < 0 || th > 6) th = 0;
    const Color m = kThemeCol[th];
    g_pal.main = lerpC(m, WHITE, 0.10f);      // accent used for active tab / switch / slider fill
    g_pal.light = lerpC(m, WHITE, 0.22f);
    g_pal.dark = lerpC(kCard, m, 0.10f);      // control backgrounds
    g_pal.search = lerpC(kBg, m, 0.12f);
    g_pal.on = lerpC(m, WHITE, 0.45f);        // focus ring
}

void doAction(int a)
{
    if (a >= 100 && a < 300)
    {
        using namespace svr06cheat;
        const int c = a % 100; const bool on = a < 200;
        if (c < kCheatCount) for (int i = 0; i < kCheats[c].count; ++i) ps2settings::set((std::string("cheat.") + kCheats[c].id + "." + std::to_string(kShop[kCheats[c].first + i].n)).c_str(), on ? 1 : 0);
        return;
    }
    switch (a)
    {
    case A_FULLSCREEN: ps2settings::set("graphics.fullscreen", ps2settings::getInt("graphics.fullscreen", 0) ? 0 : 1); break;
    case A_RESET_ALL: { ps2settings::Store &st = ps2settings::store(); std::lock_guard<std::mutex> lk(st.m); st.v.clear(); st.save(); } break;
    case A_OPEN_FOLDER:
#ifdef _WIN32
        std::system("start \"\" .");
#endif
        break;
    case A_RESET_IMG:
        for (const char *k : {"image.brightness", "image.contrast", "image.saturation", "image.vibrance", "image.gamma", "image.black", "image.temp", "image.hue", "image.sharp"}) ps2settings::reset(k);
        break;
    case A_PRESET_ORIG: ps2settings::set("graphics.filter", 0); ps2settings::set("graphics.aspect", 0); ps2settings::set("graphics.vsync", 1); doAction(A_RESET_IMG); break;
    case A_PRESET_SMOOTH: ps2settings::set("graphics.filter", 1); ps2settings::set("graphics.aspect", 0); doAction(A_RESET_IMG); break;
    case A_PRESET_ENH:
        ps2settings::set("graphics.filter", 1); ps2settings::set("graphics.aspect", 1);
        ps2settings::set("image.contrast", 8); ps2settings::set("image.saturation", 112); ps2settings::set("image.sharp", 20);
        break;
    }
}

void restartGame()
{
#ifdef _WIN32
    const std::string cmd = std::string("start \"\" ") + GetCommandLineA();
    std::system(cmd.c_str());
    std::_Exit(0);
#endif
}

bool ci_contains(const std::string &h, const std::string &n)
{
    if (n.empty()) return true;
    auto low = [](unsigned char c) { return (char)std::tolower(c); };
    std::string a, b; for (char c : h) a += low(c); for (char c : n) b += low(c);
    return a.find(b) != std::string::npos;
}

// ---------------- per-frame input + widgets
struct Ctx
{
    Rectangle view;
    Vector2 mp;
    bool click, down, rclick, inView;
    bool enter = false;      // keyboard / pad "select" this frame
    int dx = 0;              // keyboard / pad left (-1) / right (+1) this frame
    bool live = true;        // false while the menu is only fading out
} C;

float chip(const char *s, float x, float y, Color bg, Color fg, float alpha)
{
    const float size = 11;
    const Vector2 m = measure(s, size);
    const float w = m.x + L(12), h = L(18);
    rrect(Rectangle{x, y, w, h}, 9, withA(bg, 0.9f * alpha));
    text(s, x + L(6), y + (h - m.y) * 0.5f, size, withA(fg, alpha));
    return w + L(6);
}

void sliderFmt(const Item &it, double v, char *buf, size_t n)
{
    if (it.unit && it.unit[0] == '%') std::snprintf(buf, n, "%d%%", (int)std::lround(v));
    else if (it.unit && it.unit[0] == 'x') std::snprintf(buf, n, "%dx", (int)std::lround(v));
    else std::snprintf(buf, n, "%d%s", (int)std::lround(v), it.unit ? it.unit : "");
}

bool segmentedFits(const Item &it, float cw)
{
    if (it.nopts > 3) return false;
    for (int k = 0; k < it.nopts; ++k) if (measure(it.opts[k], 15).x + L(18) > cw / it.nopts) return false;
    return true;
}

float estHeight(const Item &it)
{
    switch (it.t)
    {
    case T_HEADER: return L(36);
    case T_CHECK: case T_FLAG: case T_BIND: return L(32);
    case T_VIEW: return L(150);
    case T_SLIDER: case T_COMBO: return L(62);
    case T_BUTTON: return L(40);
    default: return L(26);
    }
}

// One widget in Ship of Harkinian's style: square check boxes, label-above sliders with -/+ buttons, combo with an arrow button,
// full-width buttons, dashed section headers. Animated, keyboard / pad navigable, restart + "soon" tags.
float drawItem(int idx, const Item &it, float x, float y, float w, bool first)
{
    const bool dis = it.disabled != nullptr;
    const float alpha = dis ? 0.42f : 1.0f;
    const bool foc = C.live && g_focus == idx;
    const Color wdg = lerpC(kBg, g_pal.main, 0.55f), wdgHot = lerpC(kBg, g_pal.main, 0.80f), wdgDim = lerpC(kBg, g_pal.main, 0.30f);
    char buf[160];
    float &hv = g_hov[idx];
    float H = estHeight(it);
    auto over = [&](Rectangle r) { return C.live && C.inView && hit(r, C.mp); };
    auto setTip = [&](bool h) { if ((h || (foc && g_kbNav)) && (it.tip || dis)) { g_tipText = dis ? std::string(it.disabled) : std::string(it.tip); g_tipDisabled = dis; } };
    auto focusRing = [&](Rectangle r) { if (foc && g_kbNav) rrectL(r, 4, 2, g_pal.on); };
    auto label = [&](float lx, float ly, float lh, bool tags) {
        text(it.label, lx, textMid(ly, lh, 17), 17, withA(WHITE, alpha));
        float cx = lx + measure(it.label, 17).x + L(10);
        if (!tags) return;
        if (it.key && it.t != T_FLAG && it.t != T_BIND && !dis)
        {
            const double cur = ps2settings::get(it.key, it.def);
            if (std::fabs(cur - it.def) > 1e-9) { circle(cx + L(2), ly + lh * 0.5f, L(3), g_pal.on); cx += L(12); }
        }
        if (it.reload) cx += chip("RESTART", cx, ly + (lh - L(18)) * 0.5f, Color{120, 84, 14, 255}, kAmber, alpha);
        if (dis && it.disabled == DEV) cx += chip("SOON", cx, ly + (lh - L(18)) * 0.5f, Color{52, 56, 68, 255}, Color{190, 196, 210, 255}, 1.0f);
    };
    const bool canUse = !dis && C.live;
    if (canUse && it.t != T_HEADER && it.t != T_TEXT) g_nav.push_back(idx);
    if (g_ensure && foc && g_scrT)
    {
        const float rel = y - g_cTop;
        if (rel < L(4)) *g_scrT += rel - L(8);
        else if (rel + H > g_cH - L(4)) *g_scrT += rel + H - g_cH + L(12);
        g_ensure = false;
    }
    switch (it.t)
    {
    case T_HEADER:
    {
        const float top = first ? L(0) : L(16);
        const Vector2 m = measure(it.label, 15);
        const float ly = y + top + L(2);
        fillR(Rectangle{x, ly + m.y * 0.55f, L(12), std::max(1.0f, L(1))}, withA(WHITE, 0.30f));
        text(it.label, x + L(20), ly, 15, withA(WHITE, 0.78f));
        const float lx = x + L(20) + m.x + L(8);
        fillR(Rectangle{lx, ly + m.y * 0.55f, x + w - lx, std::max(1.0f, L(1))}, withA(WHITE, 0.18f));
        return top + m.y + L(14);
    }
    case T_TEXT:
    {
        const auto ls = wrapText(it.label, 16, w);
        const float lh = measure("Ag", 16).y + L(3);
        for (size_t i = 0; i < ls.size(); ++i) text(ls[i].c_str(), x, y + i * lh, 16, withA(WHITE, 0.62f));
        return ls.size() * lh + L(8);
    }
    case T_CHECK:
    case T_FLAG:
    {
        bool on;
        if (it.t == T_FLAG) on = (g_svr06Flags.load() & (1 << (int)it.def)) != 0; else on = ps2settings::getInt(it.key, (int)it.def) != 0;
        const float S = L(26);
        const Rectangle box{x, y + (H - S) * 0.5f, S, S}, row{x - L(4), y, w + L(8), H};
        const bool h = over(row);
        hv = approach(hv, (h || (foc && g_kbNav)) ? 1.0f : 0.0f, 18); setTip(h);
        if (hv > 0.01f) rrect(row, 5, withA(WHITE, 0.045f * hv));
        float &kn = g_knob[idx]; if (kn < 0) kn = on ? 1.0f : 0.0f; kn = approach(kn, on ? 1.0f : 0.0f, 22);
        rrect(box, 4, withA(lerpC(lerpC(wdgDim, wdgHot, hv * 0.6f), g_pal.main, kn), alpha));
        rrectL(box, 4, 1, withA(WHITE, 0.16f * alpha));
        if (kn > 0.02f)
        {
            const float s = S / 6.0f;
            const Vector2 a{box.x + s * 1.4f, box.y + S * 0.52f}, b{box.x + s * 2.6f, box.y + S * 0.72f}, c2{box.x + s * 4.7f, box.y + S * 0.30f};
            lineEx(a, b, std::max(2.0f, L(3)), withA(WHITE, 0.95f * kn * alpha)); lineEx(b, c2, std::max(2.0f, L(3)), withA(WHITE, 0.95f * kn * alpha));
        }
        label(x + S + L(10), y, H, true);
        focusRing(row);
        const bool tap = (h && C.click && !dis) || (foc && C.enter && !dis) || (foc && C.dx != 0 && !dis && ((C.dx > 0) != on));
        if (h && C.click && !dis) g_focus = idx;
        if (tap)
        {
            if (it.t == T_FLAG) { if (on) g_svr06Flags.fetch_and(~(1 << (int)it.def)); else g_svr06Flags.fetch_or(1 << (int)it.def); }
            else ps2settings::set(it.key, on ? 0 : 1);
        }
        return H;
    }
    case T_SLIDER:
    {
        const Rectangle row{x - L(4), y, w + L(8), H};
        const bool h = over(row);
        hv = approach(hv, (h || (foc && g_kbNav)) ? 1.0f : 0.0f, 18); setTip(h);
        label(x, y, L(24), true);
        double v = ps2settings::get(it.key, it.def);
        v = std::max(it.lo, std::min(it.hi, v));
        const float ty = y + L(26), th = L(28), bw = L(26);
        const Rectangle mn{x, ty, bw, th}, pl{x + w - bw, ty, bw, th}, tr{x + bw + L(5), ty, w - 2 * bw - L(10), th};
        const double step = (it.hi - it.lo) >= 100 ? (it.hi - it.lo) / 50.0 : 1.0;
        const bool hm = over(mn) && !dis, hp = over(pl) && !dis, ht = over(tr) && !dis;
        rrect(mn, 4, withA(hm ? wdgHot : wdg, alpha)); rrect(pl, 4, withA(hp ? wdgHot : wdg, alpha));
        textC("-", mn, 18, withA(WHITE, alpha)); textC("+", pl, 18, withA(WHITE, alpha));
        const float t = (float)((v - it.lo) / std::max(1e-9, it.hi - it.lo));
        rrect(tr, 4, withA(wdgDim, alpha));
        if (t > 0.002f) rrect(Rectangle{tr.x, tr.y, std::max(L(8), tr.width * t), tr.height}, 4, withA(g_pal.main, alpha));
        const float hx = tr.x + (tr.width - L(8)) * t;
        rrect(Rectangle{hx, tr.y + L(3), L(8), tr.height - L(6)}, 3, withA(WHITE, ((ht || g_dragSlider == idx) ? 0.85f : 0.55f) * alpha));
        sliderFmt(it, v, buf, sizeof buf);
        textC(buf, tr, 16, withA(WHITE, alpha));
        focusRing(Rectangle{x - L(3), ty - L(3), w + L(6), th + L(6)});
        if (!dis && C.live)
        {
            if (ht && C.click) { g_dragSlider = idx; g_focus = idx; }
            if (g_dragSlider == idx)
            {
                if (C.down)
                {
                    const float tt = std::max(0.0f, std::min(1.0f, (C.mp.x - tr.x - L(4)) / std::max(1.0f, tr.width - L(8))));
                    const double nv = std::round(it.lo + (it.hi - it.lo) * tt);
                    if (nv != std::round(v)) ps2settings::set(it.key, nv);
                }
                else g_dragSlider = -1;
            }
            if (hm && C.click) { ps2settings::set(it.key, std::max(it.lo, std::round(v - step))); g_focus = idx; }
            if (hp && C.click) { ps2settings::set(it.key, std::min(it.hi, std::round(v + step))); g_focus = idx; }
            if (h && C.rclick) ps2settings::reset(it.key);
            if (foc && C.dx != 0) ps2settings::set(it.key, std::max(it.lo, std::min(it.hi, std::round(v + step * C.dx))));
        }
        return H;
    }
    case T_COMBO:
    {
        const Rectangle row{x - L(4), y, w + L(8), H};
        const bool h = over(row);
        hv = approach(hv, (h || (foc && g_kbNav)) ? 1.0f : 0.0f, 18); setTip(h);
        label(x, y, L(24), true);
        int v = ps2settings::getInt(it.key, (int)it.def); if (v < 0 || v >= it.nopts) v = (int)it.def;
        const float cw = std::min(w, L(330)), ty = y + L(26), th = L(28);
        if (segmentedFits(it, cw))
        {
            const Rectangle box{x, ty, cw, th};
            rrect(box, 4, withA(wdgDim, alpha));
            const float sw = cw / it.nopts;
            for (int k = 0; k < it.nopts; ++k)
            {
                const Rectangle sr{box.x + sw * k + L(2), box.y + L(2), sw - L(4), box.height - L(4)};
                const bool sh = over(sr) && !dis;
                if (k == v) rrect(sr, 3, withA(g_pal.main, alpha));
                else if (sh) rrect(sr, 3, withA(wdgHot, 0.8f));
                textC(it.opts[k], sr, 15, withA(WHITE, (k == v ? 1.0f : 0.75f) * alpha));
                if (sh && C.click) { ps2settings::set(it.key, k); g_focus = idx; }
            }
            focusRing(Rectangle{x - L(3), ty - L(3), cw + L(6), th + L(6)});
            if (foc && C.dx != 0 && !dis) ps2settings::set(it.key, std::max(0, std::min(it.nopts - 1, v + C.dx)));
            if (foc && C.enter && !dis) ps2settings::set(it.key, (v + 1) % it.nopts);
        }
        else
        {
            const Rectangle box{x, ty, cw, th}, arrow{x + cw - L(30), ty, L(30), th};
            const bool bh = over(box) && !dis;
            rrect(box, 4, withA(bh || g_openCombo == idx ? wdgHot : wdg, alpha));
            rrect(arrow, 4, withA(lerpC(wdg, BLACK, 0.25f), alpha));
            text(it.opts[v], box.x + L(10), textMid(box.y, box.height, 16), 16, withA(WHITE, alpha));
            const float ax = arrow.x + arrow.width * 0.5f, ay = arrow.y + arrow.height * 0.5f;
            DrawTriangle(Vector2{ax - L(5), ay - L(3)}, Vector2{ax, ay + L(4)}, Vector2{ax + L(5), ay - L(3)}, fa(withA(WHITE, 0.9f * alpha)));
            focusRing(Rectangle{x - L(3), ty - L(3), cw + L(6), th + L(6)});
            if (bh && C.click) { g_openCombo = (g_openCombo == idx) ? -1 : idx; g_popSel = v; g_focus = idx; }
            if (foc && C.enter && !dis) { g_openCombo = (g_openCombo == idx) ? -1 : idx; g_popSel = v; }
            if (foc && C.dx != 0 && !dis && g_openCombo < 0) ps2settings::set(it.key, std::max(0, std::min(it.nopts - 1, v + C.dx)));
            g_comboCtrl = (g_openCombo == idx) ? box : g_comboCtrl;
        }
        return H;
    }
    case T_VIEW:
    {
        const int pl = it.nopts & 1;
        const uint64_t pv = g_svr06PadView[pl].load();
        const uint16_t b = (uint16_t)(pv & 0xFFFF);
        auto down = [&](int i) { return (b & svr06input::kPadBinds[i].mask) == 0; };   // active low
        const float sc = L(1), ox = x, oy = y + 4 * sc, bw = w;
        const Color off = withA(wdg, alpha), on = withA(g_pal.main, alpha);
        auto key = [&](Rectangle r, int i, const char *lab) { rrect(r, 4, down(i) ? on : off); textC(lab, r, 12, withA(WHITE, alpha)); };
        key(Rectangle{ox + 6 * sc, oy, 50 * sc, 18 * sc}, 10, "L2"); key(Rectangle{ox + 6 * sc, oy + 22 * sc, 50 * sc, 18 * sc}, 8, "L1");
        key(Rectangle{ox + bw - 56 * sc, oy, 50 * sc, 18 * sc}, 11, "R2"); key(Rectangle{ox + bw - 56 * sc, oy + 22 * sc, 50 * sc, 18 * sc}, 9, "R1");
        key(Rectangle{ox + bw * 0.5f - 44 * sc, oy + 4 * sc, 40 * sc, 16 * sc}, 14, "Select"); key(Rectangle{ox + bw * 0.5f + 4 * sc, oy + 4 * sc, 40 * sc, 16 * sc}, 15, "Start");
        const float dx = ox + 44 * sc, dy = oy + 84 * sc, q = 20 * sc;   // d-pad
        key(Rectangle{dx - q / 2, dy - q * 1.5f, q, q}, 0, "U"); key(Rectangle{dx - q / 2, dy + q * 0.5f, q, q}, 1, "D");
        key(Rectangle{dx - q * 1.5f, dy - q / 2, q, q}, 2, "L"); key(Rectangle{dx + q * 0.5f, dy - q / 2, q, q}, 3, "R");
        const float fx = ox + bw - 44 * sc, fy = dy, fr = 11 * sc, fo = 17 * sc;   // face buttons
        auto face = [&](float cx, float cy, int i, const char *lab) { circle(cx, cy, fr, down(i) ? on : off); textC(lab, Rectangle{cx - fr, cy - fr, 2 * fr, 2 * fr}, 11, withA(WHITE, alpha)); };
        face(fx, fy - fo, 7, "Tri"); face(fx, fy + fo, 4, "X"); face(fx - fo, fy, 6, "Sq"); face(fx + fo, fy, 5, "O");
        auto stick = [&](float cx, float cy, int xi, int yi, int clickBtn) {
            const float r0 = 26 * sc;
            circle(cx, cy, r0, withA(lerpC(wdg, BLACK, 0.35f), alpha));
            const float vx = (((int)((pv >> xi) & 0xFF)) - 128) / 127.f, vy = (((int)((pv >> yi) & 0xFF)) - 128) / 127.f;
            circle(cx + vx * (r0 - 6 * sc), cy + vy * (r0 - 6 * sc), 6 * sc, down(clickBtn) ? on : withA(WHITE, alpha));
        };
        stick(ox + bw * 0.5f - 44 * sc, oy + 96 * sc, 16, 24, 12); stick(ox + bw * 0.5f + 44 * sc, oy + 96 * sc, 32, 40, 13);
        return H;
    }
    case T_BIND:
    {
        const Rectangle row{x - L(4), y, w + L(8), H};
        const bool h = over(row);
        hv = approach(hv, (h || (foc && g_kbNav)) ? 1.0f : 0.0f, 18); setTip(h);
        if (hv > 0.01f) rrect(row, 5, withA(WHITE, 0.045f * hv));
        label(x, y, H, false);
        const Rectangle box{x + w * 0.46f, y + L(2), w * 0.54f, H - L(4)};
        const bool bh = over(box) && !dis;
        const bool cap = g_bindCapture == idx;
        const bool keyRow = it.nopts == 1;   // keyboard key binding row (otherwise a gamepad button row)
        int v = ps2settings::getInt(it.key, (int)it.def);
        if (!keyRow && (v < 0 || v > 17)) v = 0;
        rrect(box, 4, withA(cap ? g_pal.main : (bh ? wdgHot : wdg), alpha));
        textC(cap ? (keyRow ? "Press a key..." : "Press a button...") : (keyRow ? keyName(v) : kPadBtnName[v]), box, 15, withA(WHITE, alpha));
        focusRing(row);
        if (cap && keyRow)
        {
            int k = GetKeyPressed();
            if (k == KEY_BACKSPACE) { ps2settings::set(it.key, 0); g_bindCapture = -1; }
            else if (k == KEY_ESCAPE || GetTime() - g_bindCaptureT > 8.0) g_bindCapture = -1;
            else if (k > 0 && GetTime() - g_bindCaptureT > 0.1) { ps2settings::set(it.key, k); g_bindCapture = -1; }
        }
        else if (cap)
        {
            const int b = GetGamepadButtonPressed();
            if (b > 0 && b < 18 && GetTime() - g_bindCaptureT > 0.15) { ps2settings::set(it.key, b); g_bindCapture = -1; }
            else if (IsKeyPressed(KEY_ESCAPE) || GetTime() - g_bindCaptureT > 8.0) g_bindCapture = -1;
        }
        else if (!dis && ((bh && C.click) || (foc && C.enter))) { g_bindCapture = idx; g_bindCaptureT = GetTime(); g_focus = idx; }
        if (bh && C.rclick) { ps2settings::reset(it.key); g_bindCapture = -1; }
        return H;
    }
    case T_BUTTON:
    {
        const Rectangle r{x, y + L(4), w, H - L(8)};
        const bool h = over(r) && !dis;
        const bool danger = it.action == A_RESET_ALL;
        const bool conf = g_confirm == it.action && it.action != 0 && GetTime() - g_confirmT < 3.0;
        hv = approach(hv, (h || (foc && g_kbNav)) ? 1.0f : 0.0f, 18);
        const Color base = danger ? Color{120, 22, 30, 255} : wdg;
        rrect(r, 4, withA(lerpC(base, danger ? Color{200, 50, 60, 255} : wdgHot, hv), alpha));
        rrectL(r, 4, 1, withA(WHITE, 0.12f));
        focusRing(r);
        textC(conf ? "Click again to confirm" : it.label, r, 16, withA(WHITE, alpha));
        setTip(h);
        if (!dis && ((h && C.click) || (foc && C.enter)))
        {
            g_focus = idx;
            if (danger && !conf) { g_confirm = it.action; g_confirmT = GetTime(); }
            else { g_confirm = 0; doAction(it.action); }
        }
        return H;
    }
    }
    return 0;
}

// header icon buttons: 0 power, 1 restart, 2 close, 3 peek (eye), 4 layout (docked <-> full)
bool iconButton(Rectangle r, int kind, Color base, const char *tip, bool *heldOut = nullptr)
{
    const bool h = C.live && hit(r, C.mp);
    rrect(r, 5, h ? lerpC(base, WHITE, 0.18f) : base);
    rrectL(r, 5, 1, withA(WHITE, 0.14f));
    const Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float u = r.height * 0.24f, th = std::max(2.0f, L(2.2f));
    const Color w = withA(WHITE, 0.92f);
    if (kind == 0)
    {
        DrawRing(c, u * 0.82f, u * 0.82f + th, -50, 230, 24, fa(w));
        lineEx(Vector2{c.x, c.y - u * 1.0f}, Vector2{c.x, c.y - u * 0.1f}, th, w);
    }
    else if (kind == 1)
    {
        DrawRing(c, u * 0.9f, u * 0.9f + th, 40, 340, 24, fa(w));
        DrawTriangle(Vector2{c.x + u * 1.5f, c.y - u * 0.15f}, Vector2{c.x + u * 0.3f, c.y - u * 0.9f}, Vector2{c.x + u * 0.5f, c.y + u * 0.35f}, fa(w));
    }
    else if (kind == 2)
    {
        lineEx(Vector2{c.x - u, c.y - u}, Vector2{c.x + u, c.y + u}, th, w);
        lineEx(Vector2{c.x + u, c.y - u}, Vector2{c.x - u, c.y + u}, th, w);
    }
    else if (kind == 3)
    {
        DrawEllipseLines((int)c.x, (int)c.y, u * 1.5f, u * 0.95f, fa(w));
        DrawEllipseLines((int)c.x, (int)c.y, u * 1.5f - 1, u * 0.95f - 1, fa(w));
        circle(c.x, c.y, u * 0.5f, w);
        if (heldOut) *heldOut = h && C.down;
    }
    else
    {
        const float q = u * 1.1f;
        rrectL(Rectangle{c.x - q, c.y - q * 0.8f, q * 2, q * 1.6f}, 2, 1.6f, w);
        fillR(Rectangle{c.x + q * 0.1f, c.y - q * 0.8f, q * 0.9f, q * 1.6f}, withA(WHITE, 0.55f));
    }
    if (h) { g_tipText = tip; g_tipDisabled = false; }
    return h && C.click;
}

void closeMenuNow() { g_svr06MenuOpen.store(false); if (g_wasHidden) HideCursor(); g_searchFocus = false; g_openCombo = -1; g_confirm = 0; }

} // namespace

// ---------------- image adjustment shader
namespace
{
Shader g_sh{};
bool g_shTried = false, g_shOk = false;
int g_locA = -1, g_locB = -1, g_locC = -1;
bool g_adjActive = false;
const char *kFs =
    "#version 330\n"
    "in vec2 fragTexCoord; in vec4 fragColor; out vec4 finalColor;\n"
    "uniform sampler2D texture0; uniform vec4 colDiffuse;\n"
    "uniform vec4 adjA; uniform vec4 adjB; uniform vec4 adjC;\n"
    "void main(){\n"
    "  vec3 c = texture(texture0, fragTexCoord).rgb;\n"
    "  if (adjC.x > 0.0) { vec2 t = adjC.yz; vec3 n = texture(texture0, fragTexCoord+vec2(t.x,0)).rgb + texture(texture0, fragTexCoord-vec2(t.x,0)).rgb + texture(texture0, fragTexCoord+vec2(0,t.y)).rgb + texture(texture0, fragTexCoord-vec2(0,t.y)).rgb; c += (c - n*0.25) * adjC.x * 2.0; }\n"
    "  c = (c - adjB.z) / max(1.0 - adjB.z, 0.01);\n"
    "  c = pow(max(c, 0.0), vec3(1.0 / adjA.w));\n"
    "  c += adjA.x;\n"
    "  c = (c - 0.5) * adjA.y + 0.5;\n"
    "  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "  float mx = max(c.r, max(c.g, c.b)); float mn = min(c.r, min(c.g, c.b)); float sat = mx - mn;\n"
    "  float s = adjA.z * (1.0 + adjB.w * (1.0 - sat));\n"
    "  c = mix(vec3(l), c, s);\n"
    "  float ch = cos(adjB.x), sh = sin(adjB.x);\n"
    "  mat3 rgb2yiq = mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312);\n"
    "  mat3 yiq2rgb = mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703);\n"
    "  vec3 y = rgb2yiq * c; y = vec3(y.x, y.y*ch - y.z*sh, y.y*sh + y.z*ch); c = yiq2rgb * y;\n"
    "  c += vec3(adjB.y * 0.12, 0.0, -adjB.y * 0.12);\n"
    "  finalColor = vec4(clamp(c, 0.0, 1.0), 1.0) * colDiffuse * fragColor;\n"
    "}\n";
} // namespace

void svr06_adjust_begin(int texW, int texH)
{
    g_adjActive = false;
    const float br = ps2settings::getFloat("image.brightness", 0) / 100.0f * 0.5f;
    const float co = 1.0f + ps2settings::getFloat("image.contrast", 0) / 100.0f;
    const float sa = ps2settings::getFloat("image.saturation", 100) / 100.0f;
    const float ga = std::max(0.2f, ps2settings::getFloat("image.gamma", 100) / 100.0f);
    const float hu = ps2settings::getFloat("image.hue", 0) * 3.14159265f / 180.0f;
    const float te = ps2settings::getFloat("image.temp", 0) / 100.0f;
    const float bl = ps2settings::getFloat("image.black", 0) / 100.0f * 0.5f;
    const float vi = ps2settings::getFloat("image.vibrance", 0) / 100.0f;
    const float sp = ps2settings::getFloat("image.sharp", 0) / 100.0f;
    const bool neutral = br == 0 && co == 1.0f && sa == 1.0f && ga == 1.0f && hu == 0 && te == 0 && bl == 0 && vi == 0 && sp == 0;
    if (neutral) return;
    if (!g_shTried)
    {
        g_shTried = true;
        g_sh = LoadShaderFromMemory(nullptr, kFs);
        g_shOk = IsShaderValid(g_sh);
        if (g_shOk) { g_locA = GetShaderLocation(g_sh, "adjA"); g_locB = GetShaderLocation(g_sh, "adjB"); g_locC = GetShaderLocation(g_sh, "adjC"); }
        std::fprintf(stderr, "[menu] image-adjust shader %s\n", g_shOk ? "ok" : "FAILED");
    }
    if (!g_shOk) return;
    const float a[4] = {br, co, sa, ga}, b[4] = {hu, te, bl, vi}, c[4] = {sp, 1.0f / std::max(1, texW), 1.0f / std::max(1, texH), 0};
    SetShaderValue(g_sh, g_locA, a, SHADER_UNIFORM_VEC4);
    SetShaderValue(g_sh, g_locB, b, SHADER_UNIFORM_VEC4);
    SetShaderValue(g_sh, g_locC, c, SHADER_UNIFORM_VEC4);
    BeginShaderMode(g_sh);
    g_adjActive = true;
}
void svr06_adjust_end()
{
    if (g_adjActive) EndShaderMode();
    g_adjActive = false;
}

// per-frame: window-level settings (vsync) + menu toggle + draw
void svr06_menu_frame()
{
    // vsync follows the setting
    {
        static int s_vs = -1;
        const int want = ps2settings::getInt("graphics.vsync", 1);
        if (want != s_vs)
        {
            if (want) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
            s_vs = want;
        }
    }
    g_dt = std::min(0.05f, std::max(0.001f, GetFrameTime()));
    const bool wasOpen = g_svr06MenuOpen.load();
    bool open = wasOpen;
    bool padBack = false;
    for (int gi = 0; gi < 4; ++gi) if (IsGamepadAvailable(gi) && IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) padBack = true;
    if (IsKeyPressed(KEY_F1) && !g_searchFocus) open = !open;
    if (open && (IsKeyPressed(KEY_ESCAPE) || (padBack && g_bindCapture < 0)))
    {
        if (g_bindCapture >= 0) g_bindCapture = -1;
        else if (g_openCombo >= 0) g_openCombo = -1;
        else if (g_searchFocus) { g_searchFocus = false; if (!g_search.empty() && IsKeyPressed(KEY_ESCAPE)) g_search.clear(); }
        else open = false;
    }
    if (open && !wasOpen) { g_wasHidden = IsCursorHidden(); if (g_wasHidden) ShowCursor(); g_openCombo = -1; g_dragSlider = -1; g_focus = -1; g_kbNav = false; }
    if (!open && wasOpen) { if (g_wasHidden) HideCursor(); g_searchFocus = false; g_confirm = 0; g_dragSlider = -1; g_openCombo = -1; }
    g_svr06MenuOpen.store(open);
    g_vis = approach(g_vis, open ? 1.0f : 0.0f, 16);
    if (!open && g_vis < 0.01f) { g_vis = 0; return; }

    buildItems();
    if (g_hov.size() != g_items.size()) { g_hov.assign(g_items.size(), 0.0f); g_knob.assign(g_items.size(), -1.0f); }
    if (!g_fontOk)
    {
        static bool tried = false;
        if (!tried)
        {
            tried = true;
            g_font = LoadFontFromMemory(".ttf", kShipFont, (int)sizeof(kShipFont), 64, nullptr, 0);
            g_fontOk = g_font.texture.id != 0;
            if (g_fontOk) SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
        }
    }
    if (g_startVals.empty())      // remember what the game started with so "restart required" settings can be flagged
        for (const Item &it : g_items) if (it.reload && it.key) g_startVals[it.key] = ps2settings::get(it.key, it.def);
    applyPalette();

    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    g_reduceMotion = ps2settings::getInt("menu.reduceMotion", 0) != 0; g_highContrast = ps2settings::getInt("menu.highContrast", 0) != 0; g_boldText = ps2settings::getInt("menu.boldText", 0) != 0;
    const float userScale = std::max(0.5f, ps2settings::getFloat("menu.scale", 100) / 100.0f);
    g_S = (H / 1080.0f) * userScale;
    if (W / H < 1.2f) g_S = std::min(g_S, W / 1500.0f * userScale);

    C.live = open;
    C.mp = GetMousePosition();
    C.click = open && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    C.down = open && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    C.rclick = open && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    C.enter = false; C.dx = 0;
    g_tipText.clear();
    if (g_confirm && GetTime() - g_confirmT > 3.0) g_confirm = 0;
    {
        const Vector2 md = GetMouseDelta();
        if (md.x != 0 || md.y != 0 || C.click) g_kbNav = false;
    }

    // ---- keyboard / gamepad navigation (arrows + Enter, D-pad + A, LB/RB tabs, PgUp/PgDn tabs, [ ] or X/Y pages)
    int dy = 0, dTab = 0, dSide = 0;
    if (open && !g_searchFocus && g_bindCapture < 0 && g_vis > 0.5f)
    {
        auto pr = [](int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); };
        if (pr(KEY_DOWN)) dy = 1;
        if (pr(KEY_UP)) dy = -1;
        if (pr(KEY_LEFT)) C.dx = -1;
        if (pr(KEY_RIGHT)) C.dx = 1;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) C.enter = true;
        if (IsKeyPressed(KEY_PAGE_UP)) dTab = -1;
        if (IsKeyPressed(KEY_PAGE_DOWN)) dTab = 1;
        if (IsKeyPressed(KEY_LEFT_BRACKET)) dSide = -1;
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) dSide = 1;
        for (int gi = 0; gi < 4; ++gi)
        {
            if (!IsGamepadAvailable(gi)) continue;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) dTab = -1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) dTab = 1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_UP)) dy = -1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) dy = 1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) C.dx = -1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) C.dx = 1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) C.enter = true;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) dSide = -1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_UP)) dSide = 1;
        }
    }
    if (dTab) { g_tab = (g_tab + 4 + dTab) % 4; g_search.clear(); g_openCombo = -1; g_focus = -1; g_kbNav = true; }
    if (dSide && g_search.empty()) { g_side[g_tab] = (g_side[g_tab] + kSideN[g_tab] + dSide) % kSideN[g_tab]; g_openCombo = -1; g_focus = -1; g_kbNav = true; }
    if (g_openCombo >= 0)
    {
        const Item &oc = g_items[g_openCombo];
        if (dy) g_popSel = std::max(0, std::min(oc.nopts - 1, g_popSel + dy));
        if (C.enter) { ps2settings::set(oc.key, g_popSel); g_openCombo = -1; }
        C.enter = false; C.dx = 0; dy = 0;
    }
    else if (dy || C.dx || C.enter)
    {
        g_kbNav = true;
        if (dy)
        {
            int pos = -1;
            for (size_t i = 0; i < g_nav.size(); ++i) if (g_nav[i] == g_focus) pos = (int)i;
            if (!g_nav.empty())
            {
                if (pos < 0) pos = dy > 0 ? 0 : (int)g_nav.size() - 1;
                else pos = std::max(0, std::min((int)g_nav.size() - 1, pos + dy));
                g_focus = g_nav[pos]; g_ensure = true;
            }
        }
        else if (g_focus < 0 && !g_nav.empty()) { g_focus = g_nav[0]; g_ensure = true; C.enter = false; C.dx = 0; }
    }
    g_nav.clear();

    // ---- panel geometry
    const int lay = ps2settings::getInt("menu.layout", 0);        // 0 = docked (game stays visible), 1 = full (Ship of Harkinian layout)
    const float ease = 1.0f - std::pow(1.0f - g_vis, 3.0f);
    const bool peekOpt = ps2settings::getInt("menu.autoPeek", 1) != 0;
    g_peek = approach(g_peek, ((g_dragSlider >= 0 && peekOpt) ? 1.0f : 0.0f), 12);
    static bool s_eye = false;
    if (s_eye) g_peek = approach(g_peek, 1.0f, 20);
    g_A = ease * (1.0f - 0.86f * g_peek);
    Rectangle panel;
    if (lay == 1) { const float pw = W * 0.90f; panel = Rectangle{(W - pw) * 0.5f, H * 0.035f, pw, H * 0.93f}; }
    else
    {
        const float pw = std::min(W - L(24), L(940)), ph = std::min(H - L(32), L(860));
        panel = Rectangle{W - pw - L(16), L(16), pw, ph};
    }
    panel.x += (1.0f - ease) * L(lay == 1 ? 0.0f : 70.0f);
    panel.y += (1.0f - ease) * L(lay == 1 ? 24.0f : 0.0f);
    const float opacity = g_highContrast ? 1.0f : std::max(0.0f, std::min(1.0f, ps2settings::getFloat("menu.opacity", 88) / 100.0f));
    // soft shadow + body
    for (int i = 3; i >= 1; --i) rrect(Rectangle{panel.x - L(5.0f * i), panel.y - L(3.0f * i), panel.width + L(10.0f * i), panel.height + L(10.0f * i)}, 10 + i * 4, withA(BLACK, 0.10f));
    rrect(panel, 10, withA(kBg, opacity));
    rrectL(panel, 10, 1, withA(WHITE, 0.16f));
    const float pad = L(18);
    const float left = panel.x + pad, right = panel.x + panel.width - pad;

    // ---- top bar: tabs, search, system buttons
    const float barY = panel.y + L(14), barH = L(40);
    {
        float tx = left;
        for (int i = 0; i < 4; ++i)
        {
            const Vector2 m = measure(kTabNames[i], 18);
            const Rectangle r{tx, barY, m.x + L(30), barH};
            const bool h = C.live && hit(r, C.mp);
            const bool act = (i == g_tab) && g_search.empty();
            if (act) rrect(r, 5, g_pal.main);
            else if (h) rrect(r, 5, withA(g_pal.main, 0.45f));
            textC(kTabNames[i], r, 18, act ? WHITE : withA(WHITE, 0.92f));
            if (h && C.click) { g_tab = i; g_search.clear(); g_openCombo = -1; g_focus = -1; }
            tx += r.width + L(4);
        }
        const float s = barH;
        Rectangle rClose{right - s, barY, s, s}, rRest{rClose.x - s - L(6), barY, s, s}, rPow{rRest.x - s - L(6), barY, s, s};
        Rectangle rLay{rPow.x - s - L(14), barY, s, s}, rEye{rLay.x - s - L(6), barY, s, s};
        const Color red{150, 22, 30, 255}, grey{86, 88, 98, 255}, blue = lerpC(kBg, g_pal.main, 0.55f);
        const bool c1 = iconButton(rPow, 0, red, g_confirm == 1001 ? "Click again to quit the game" : "Quit the game");
        const bool c2 = iconButton(rRest, 1, red, g_confirm == 1002 ? "Click again to restart the game" : "Restart the game");
        const bool c3 = iconButton(rClose, 2, grey, "Close this menu (F1)");
        const bool c4 = iconButton(rLay, 4, blue, lay == 1 ? "Switch to the docked layout (keeps the game visible)" : "Switch to the full-size layout");
        bool held = false;
        iconButton(rEye, 3, blue, "Hold to fade the menu and see the game behind it", &held);
        s_eye = held;
        if (c1) { if (g_confirm == 1001) std::_Exit(0); g_confirm = 1001; g_confirmT = GetTime(); }
        if (c2) { if (g_confirm == 1002) restartGame(); g_confirm = 1002; g_confirmT = GetTime(); }
        if (c3) closeMenuNow();
        if (c4) ps2settings::set("menu.layout", lay == 1 ? 0 : 1);
        // search box between the tabs and the icons
        const float sx0 = tx + L(10), sx1 = rEye.x - L(14);
        if (sx1 - sx0 > L(90))
        {
            const Rectangle sb{sx0, barY, sx1 - sx0, barH};
            rrect(sb, 5, g_pal.search);
            rrectL(sb, 5, 1, g_searchFocus ? withA(WHITE, 0.55f) : withA(WHITE, 0.14f));
            if (C.click) g_searchFocus = hit(sb, C.mp);
            if (g_searchFocus && open)
            {
                int ch;
                while ((ch = GetCharPressed()) > 0) if (ch >= 32 && ch < 127 && g_search.size() < 40) { g_search += (char)ch; g_scroll[g_tab][7] = g_scrollT[g_tab][7] = 0; }
                if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !g_search.empty()) g_search.pop_back();
            }
            // magnifier
            DrawRing(Vector2{sb.x + L(18), sb.y + barH * 0.46f}, L(5), L(6.6f), 0, 360, 20, fa(withA(WHITE, 0.55f)));
            lineEx(Vector2{sb.x + L(22), sb.y + barH * 0.46f + L(4)}, Vector2{sb.x + L(27), sb.y + barH * 0.46f + L(9)}, L(2), withA(WHITE, 0.55f));
            const bool empty = g_search.empty();
            std::string shown = empty ? "Search..." : g_search;
            if (g_searchFocus && ((int)(GetTime() * 2) & 1)) shown += "|";
            text(shown.c_str(), sb.x + L(38), textMid(sb.y, barH, 17), 17, withA(WHITE, empty && !g_searchFocus ? 0.4f : 0.95f));
        }
    }
    // rule under the bar
    const float ruleY = barY + barH + L(10);
    fillR(Rectangle{left, ruleY, right - left, std::max(2.0f, L(2.5f))}, withA(WHITE, 0.92f));

    // ---- footer geometry (tip / restart banner / key hints)
    const float footH = L(84);
    const float footY = panel.y + panel.height - footH - L(8);
    std::string pending;
    for (const Item &it : g_items)
        if (it.reload && it.key)
        {
            auto f = g_startVals.find(it.key);
            if (f != g_startVals.end() && std::fabs(f->second - ps2settings::get(it.key, it.def)) > 1e-9) { if (!pending.empty()) pending += ", "; pending += it.label; }
        }

    // ---- sidebar
    const float sideW = L(lay == 1 ? 210 : 160), bodyY = ruleY + L(14);
    if (g_search.empty())
    {
        float sy = bodyY;
        for (int i = 0; i < kSideN[g_tab]; ++i)
        {
            const Rectangle r{left, sy, sideW, L(40)};
            const bool h = C.live && hit(r, C.mp), sel = g_side[g_tab] == i;
            if (sel) rrect(r, 5, g_pal.main); else if (h) rrect(r, 5, withA(g_pal.main, 0.40f));
            textC(kSides[g_tab][i], r, 18, sel ? WHITE : withA(WHITE, 0.9f));
            if (h && C.click) { g_side[g_tab] = i; g_openCombo = -1; g_focus = -1; }
            sy += L(40) + L(6);
        }
    }
    else
    {
        Rectangle r{left, bodyY, sideW, L(40)};
        textC("Search results", r, 17, withA(WHITE, 0.75f));
    }
    const float divX = left + sideW + L(12);
    fillR(Rectangle{divX, bodyY, std::max(2.0f, L(2.5f)), footY - bodyY - L(6)}, withA(WHITE, 0.92f));

    // ---- content (multi-column, groups balanced across the columns like Ship's menu)
    const Rectangle content{divX + L(16), bodyY, right - (divX + L(16)), footY - bodyY - L(6)};
    C.view = content;
    C.inView = C.live && hit(content, C.mp) && g_openCombo < 0;
    g_cTop = content.y; g_cH = content.height;
    const int sideIdx = g_search.empty() ? g_side[g_tab] : 7;
    float &scrollT = g_scrollT[g_tab][sideIdx];
    float &scroll = g_scroll[g_tab][sideIdx];
    if (C.inView) scrollT -= GetMouseWheelMove() * L(70);
    g_scrT = &scrollT;
    scroll = approach(scroll, scrollT, 18);
    if (std::fabs(scroll - scrollT) < 0.3f) scroll = scrollT;
    if (scroll < 0) { scroll = 0; if (scrollT < 0) scrollT = 0; }

    const float gap = L(28), colMin = L(330);
    int ncol = std::max(1, std::min(3, (int)std::floor((content.width - L(10) + gap) / (colMin + gap))));
    const float colW = (content.width - L(14) - gap * (ncol - 1)) / ncol;
    // collect the items of this page, grouped by header
    std::vector<int> order;
    if (g_search.empty())
    {
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < g_items.size(); ++i)
                if (g_items[i].tab == g_tab && g_items[i].side == g_side[g_tab] && g_items[i].col == pass) order.push_back((int)i);
    }
    else
        for (size_t i = 0; i < g_items.size(); ++i)
        {
            const Item &it = g_items[i];
            if (it.t == T_HEADER || it.t == T_TEXT) continue;
            if (ci_contains(it.label, g_search) || (it.tip && ci_contains(it.tip, g_search))) order.push_back((int)i);
        }
    struct Grp { std::vector<int> it; float est = 0; };
    std::vector<Grp> grps;
    for (int i : order)
    {
        if (g_items[i].t == T_HEADER || grps.empty() || !g_search.empty()) grps.emplace_back();
        grps.back().it.push_back(i); grps.back().est += estHeight(g_items[i]);
    }
    std::vector<std::vector<int>> colGroups(ncol);
    std::vector<float> colEst(ncol, 0.0f);
    for (size_t g = 0; g < grps.size(); ++g)
    {
        int c = 0; for (int k = 1; k < ncol; ++k) if (colEst[k] < colEst[c] - 1.0f) c = k;
        colGroups[c].push_back((int)g); colEst[c] += grps[g].est + L(6);
    }
    BeginScissorMode((int)content.x, (int)content.y, (int)content.width, (int)content.height);
    float maxY = 0;
    for (int c = 0; c < ncol; ++c)
    {
        float y = content.y + L(2) - scroll;
        const float x = content.x + c * (colW + gap);
        bool firstInCol = true;
        for (int g : colGroups[c])
        {
            for (int i : grps[g].it) { y += drawItem(i, g_items[i], x, y, colW, firstInCol); firstInCol = false; }
            y += L(6);
        }
        maxY = std::max(maxY, y + scroll - content.y);
    }
    if (grps.empty()) text(g_search.empty() ? "Nothing here." : "No matching options.", content.x, content.y + L(4), 17, withA(WHITE, 0.6f));
    EndScissorMode();
    {
        const float maxS = std::max(0.0f, maxY - content.height + L(16));
        if (scrollT > maxS) scrollT = maxS;
        if (scroll > maxS + 1) scroll = maxS;
        if (maxS > 0)
        {
            const float th = std::max(L(30), content.height * content.height / (content.height + maxS));
            const float ty = content.y + (content.height - th) * (scroll / maxS);
            rrect(Rectangle{content.x + content.width - L(6), ty, L(5), th}, 2, withA(WHITE, 0.30f));
        }
    }

    // ---- open dropdown (drawn above everything, unclipped)
    if (g_openCombo >= 0)
    {
        const Item &it = g_items[g_openCombo];
        const float oh = L(30);
        const float ph = oh * it.nopts;
        float py = g_comboCtrl.y + g_comboCtrl.height + L(2);
        if (py + ph > H - L(8)) py = g_comboCtrl.y - ph - L(2);
        const Rectangle pr{g_comboCtrl.x, py, g_comboCtrl.width, ph};
        rrect(Rectangle{pr.x - 2, pr.y - 2, pr.width + 4, pr.height + 4}, 5, withA(BLACK, 0.85f));
        rrect(pr, 4, lerpC(kBg, g_pal.main, 0.35f));
        const int cur = ps2settings::getInt(it.key, (int)it.def);
        bool closeIt = false;
        for (int k = 0; k < it.nopts; ++k)
        {
            const Rectangle r{pr.x, pr.y + k * oh, pr.width, oh};
            const bool h = C.live && hit(r, C.mp);
            if (h) { Vector2 md = GetMouseDelta(); if (md.x != 0 || md.y != 0) g_popSel = k; }
            if (k == g_popSel) rrect(Rectangle{r.x + L(2), r.y + L(2), r.width - L(4), r.height - L(4)}, 3, g_pal.main);
            text(it.opts[k], r.x + L(10), textMid(r.y, oh, 16), 16, k == cur ? WHITE : withA(WHITE, 0.82f));
            if (h && C.click) { ps2settings::set(it.key, k); closeIt = true; }
        }
        if (C.click && !hit(pr, C.mp) && !hit(g_comboCtrl, C.mp)) closeIt = true;
        if (closeIt) g_openCombo = -1;
    }

    // ---- footer: restart banner, hovered option description, key hints
    {
        fillR(Rectangle{left, footY, right - left, std::max(1.0f, L(1))}, withA(WHITE, 0.22f));
        const float tx = left, ty = footY + L(8);
        if (!pending.empty())
        {
            const Rectangle br{left, ty, right - left, L(30)};
            rrect(br, 5, Color{86, 60, 8, 255});
            rrectL(br, 5, 1, withA(kAmber, 0.6f));
            const Rectangle rb{br.x + br.width - L(130), br.y + L(3), L(124), br.height - L(6)};
            const bool bh = C.live && hit(rb, C.mp);
            rrect(rb, 4, bh ? Color{230, 160, 40, 255} : Color{190, 130, 30, 255});
            textC(g_confirm == 1002 ? "Click again" : "Restart now", rb, 15, BLACK);
            if (bh && C.click) { if (g_confirm == 1002) restartGame(); g_confirm = 1002; g_confirmT = GetTime(); }
            std::string msg = "Restart to apply: " + pending;
            const float maxW = br.width - L(160);
            while (measure(msg.c_str(), 15).x > maxW && msg.size() > 12) { msg.resize(msg.size() - 4); msg += "..."; }
            text(msg.c_str(), br.x + L(12), textMid(br.y, br.height, 15), 15, kAmber);
        }
        else
        {
            const std::string tip = g_tipText.empty() ? std::string("Hover an option to see what it does. Settings are saved as you change them and shared with the launcher.") : g_tipText;
            const auto ls = wrapText(tip, 15, right - left);
            const float lh = measure("Ag", 15).y + L(2);
            for (size_t i = 0; i < ls.size() && i < 3; ++i)
                text(ls[i].c_str(), tx, ty + i * lh, 15, g_tipText.empty() ? withA(WHITE, 0.45f) : (g_tipDisabled ? kAmber : withA(WHITE, 0.92f)));
        }
        const char *keys = lay == 1 ? "Up/Down select   Left/Right adjust   Enter toggle   PgUp/PgDn tabs   [ ] pages   F1 close"
                                    : "Up/Down  Left/Right  Enter  PgUp/PgDn tabs  [ ] pages  F1 close";
        textR(keys, right, footY + footH - L(20), 12, withA(WHITE, 0.38f));
    }
}
