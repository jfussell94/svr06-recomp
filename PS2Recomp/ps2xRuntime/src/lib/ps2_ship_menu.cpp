// SvR06 in-game settings menu - layout/look follows the Ship of Harkinian (libultraship) menu:
// top tab row, search box, red/red/grey system buttons, white rule, centered sidebar, two content columns,
// blue widgets with labels above sliders/combos, tooltips, greyed items with a reason.
// Pure raylib (no ImGui). Montserrat Regular (SIL OFL 1.1) is embedded.
#include "raylib.h"
#include "ps2_settings.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
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
}
extern std::atomic<int> g_svr06Flags;

namespace
{
enum Type { T_HEADER, T_CHECK, T_SLIDER, T_COMBO, T_BUTTON, T_TEXT, T_FLAG, T_BIND };
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
const char *kSide0[] = {"General", "Audio", "Graphics", "Controls", "Presets"};
const char *kSide1[] = {"Gameplay", "Interface", "Music"};
const char *kSide2[] = {"Cheats"};
const char *kSide3[] = {"Debug Flags", "Info"};
const char *const *kSides[4] = {kSide0, kSide1, kSide2, kSide3};
const int kSideN[4] = {5, 3, 1, 2};

const char *kThemeNames[] = {"Blue", "Indigo", "Purple", "Red", "Orange", "Green", "Gray"};
const Color kThemeCol[] = {{0, 61, 204, 255}, {63, 51, 190, 255}, {120, 40, 170, 255}, {190, 25, 25, 255}, {210, 105, 10, 255}, {20, 130, 60, 255}, {90, 90, 100, 255}};
const char *kFilt[] = {"Nearest (sharp)", "Bilinear (smooth)"};
const char *kAsp[] = {"4:3 (original)", "16:9", "21:9", "Stretch"};
const char *kRend[] = {"Vulkan (paraLLEl-GS)", "Software (reference)"};
const char *kCam[] = {"Original", "Wide", "Close"};
const char *kAi[] = {"Original", "Easy", "Hard"};
const char *kInterp[] = {"Gaussian (original)", "Linear"};

const char *DEV = "Not implemented yet (in development).";
const char *kDev[] = {"Auto", "Gamepad 1", "Gamepad 2", "Gamepad 3", "Gamepad 4", "None"};
const char *kPadBtnName[18] = {"(none)", "D-Pad Up", "D-Pad Right", "D-Pad Down", "D-Pad Left", "Y / Triangle", "B / Circle", "A / Cross",
                                "X / Square", "LB / L1", "LT / L2", "RB / R1", "RT / R2", "Back / Select", "Guide", "Start", "Left Stick Click", "Right Stick Click"};
const char *kPs2Label[16] = {"Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Select", "Start"};
int g_bindCapture = -1;   // item index waiting for a gamepad button
double g_bindCaptureT = 0;
std::deque<std::string> g_keyStore; // stable storage for generated setting keys
const char *K(const char *fmt, int a, const char *b = "")
{
    char buf[96]; std::snprintf(buf, sizeof buf, fmt, a, b); g_keyStore.emplace_back(buf); return g_keyStore.back().c_str();
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
    I(0, 0, 0, T_SLIDER, "Menu Background Opacity", "menu.opacity", 85, 0, 100, "%", nullptr, 0, "Sets the opacity of the menu background.");
    I(0, 0, 0, T_SLIDER, "Menu Scale", "menu.scale", 100, 70, 130, "%", nullptr, 0, "Scales the whole menu.");
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
    I(0, 2, 0, T_SLIDER, "Internal Resolution", "graphics.resPct", 100, 50, 200, "%", nullptr, 0, "Multiplies the render resolution (super-sampling).", "Needs the Vulkan renderer's SSAA path (in development).");
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
        I(0, 3, pl, T_COMBO, "Controller", K("input.p%d.device", pl + 1), 0, 0, 0, nullptr, kDev, 6,
          "Auto: Player 1 takes the first connected gamepad, Player 2 the second. Pick a number to pin a pad; None disconnects this player's pad.");
        I(0, 3, pl, T_CHECK, "Keyboard", K("input.p%d.keyboard", pl + 1), pl == 0 ? 1 : 0, 0, 1, nullptr, nullptr, 0,
          "Keyboard also drives this player: arrows/WASD, X/Space = Cross, C = Circle, Z = Square, V = Triangle, Q/E = L1/R1, Shift = L2/R2, Enter = Start, Tab = Select.");
        I(0, 3, pl, T_CHECK, "Left Stick Moves D-Pad", K("input.p%d.stickdpad", pl + 1), 1, 0, 1, nullptr, nullptr, 0, "The left stick also presses the D-pad directions (the game moves wrestlers with both).");
        for (int i = 0; i < 16; ++i)
            I(0, 3, pl, T_BIND, kPs2Label[i], K("input.p%d.btn.%s", pl + 1, svr06input::kPadBinds[i].name), svr06input::kPadBinds[i].defBtn, 0, 0, nullptr, nullptr, 0,
              "Click, then press the gamepad button to use. Right-click resets to the default. Esc cancels.");
    }
    I(0, 3, 0, T_SLIDER, "Stick Deadzone", "controls.deadzone", 15, 0, 50, "%", nullptr, 0, "Stick movement inside this range counts as centred (both players).");
    I(0, 3, 1, T_CHECK, "Rumble", "controls.rumble", 1, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    // ---- Settings / Presets
    I(0, 4, 0, T_HEADER, "Presets");
    I(0, 4, 0, T_BUTTON, "Apply: Original 1:1", nullptr, 0, 0, 0, nullptr, nullptr, 0, "4:3, nearest filter, neutral image, vsync on.", nullptr, false, A_PRESET_ORIG);
    I(0, 4, 0, T_BUTTON, "Apply: Smooth", nullptr, 0, 0, 0, nullptr, nullptr, 0, "Bilinear filter, neutral image.", nullptr, false, A_PRESET_SMOOTH);
    I(0, 4, 0, T_BUTTON, "Apply: Enhanced", nullptr, 0, 0, 0, nullptr, nullptr, 0, "16:9, bilinear filter, slight contrast, saturation and sharpness.", nullptr, false, A_PRESET_ENH);
    // ---- Enhancements
    I(1, 0, 0, T_HEADER, "Gameplay");
    I(1, 0, 0, T_CHECK, "Skip Intro Movies", "game.skipMovies", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
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
    // ---- Cheats
    I(2, 0, 0, T_HEADER, "Cheats");
    I(2, 0, 0, T_CHECK, "Unlock All Shop Items", "cheat.unlockAll", 0, 0, 1, nullptr, nullptr, 0, "Unlocks everything in the shop (Legends etc.).");
    I(2, 0, 0, T_CHECK, "Infinite Money", "cheat.money", 0, 0, 1, nullptr, nullptr, 0, "Shop items cost nothing.");
    I(2, 0, 0, T_CHECK, "Unlock All Content", "game.unlockAll", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(2, 0, 1, T_HEADER, "In The Ring");
    I(2, 0, 1, T_CHECK, "God Mode", "cheat.god", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(2, 0, 1, T_CHECK, "Infinite Spirit / Stamina", "cheat.spirit", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(2, 0, 1, T_CHECK, "No Stun", "cheat.noStun", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
    I(2, 0, 1, T_COMBO, "CPU Difficulty Override", "cheat.ai", 0, 0, 0, nullptr, kAi, 3, nullptr, DEV);
    I(2, 0, 1, T_CHECK, "Save States (F5/F9)", "cheat.savestates", 0, 0, 1, nullptr, nullptr, 0, nullptr, DEV);
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
float g_scroll[4][8] = {};
bool g_searchFocus = false;
std::string g_search;
int g_openCombo = -1;       // item index of the open combo
int g_dragSlider = -1;
int g_confirm = 0;          // action awaiting confirmation (1000+ = system buttons)
double g_confirmT = 0;
std::string g_tipText;
bool g_tipDisabled = false;
bool g_wasHidden = false;

struct Pal { Color main, light, dark, search, on; } g_pal;

Color lerpC(Color a, Color b, float t) { return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), 255}; }
Color mulC(Color a, float f) { return Color{(unsigned char)std::min(255.f, a.r * f), (unsigned char)std::min(255.f, a.g * f), (unsigned char)std::min(255.f, a.b * f), 255}; }
Color withA(Color c, float a) { c.a = (unsigned char)std::max(0.f, std::min(255.f, a * 255.f)); return c; }

float g_S = 1, g_ox = 0, g_oy = 0;
float X(float v) { return g_ox + v * g_S; }
float Y(float v) { return g_oy + v * g_S; }
float L(float v) { return v * g_S; }

void text(const char *s, float x, float y, float size, Color c)
{
    if (g_fontOk) DrawTextEx(g_font, s, Vector2{x, y}, size * g_S, 0, c);
    else DrawText(s, (int)x, (int)y, (int)(size * g_S), c);
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
void rrect(Rectangle r, float radius, Color c)
{
    const float rr = std::min(r.width, r.height) * 0.5f;
    DrawRectangleRounded(r, std::min(1.0f, L(radius) / std::max(1.0f, rr)), 6, c);
}
bool hit(Rectangle r, Vector2 p) { return CheckCollisionPointRec(p, r); }

void applyPalette()
{
    int th = ps2settings::getInt("menu.theme", 0);
    if (th < 0 || th > 6) th = 0;
    const Color m = kThemeCol[th];
    g_pal.main = m;
    g_pal.light = lerpC(m, WHITE, 0.10f);
    g_pal.dark = mulC(m, 0.85f);
    g_pal.search = mulC(m, 0.64f);
    g_pal.on = lerpC(m, WHITE, 0.25f);
}

void doAction(int a)
{
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

// ---------------- widgets. All return nothing; y is advanced. Rects are in screen pixels.
struct Ctx
{
    Rectangle view;      // clip rect for hit tests
    Vector2 mp;
    bool click, down, rclick, inView;
    int hover = -1;
} C;

float drawItem(int idx, const Item &it, float x, float y, float w)
{
    const float H = L(36), gap = L(15);
    const bool dis = it.disabled != nullptr;
    const float alpha = dis ? 0.45f : 1.0f;
    char buf[160];
    auto hov = [&](Rectangle r) {
        const bool h = C.inView && hit(r, C.mp);
        if (h)
        {
            if (it.tip || dis) { g_tipText = dis ? std::string("Disabled: ") + it.disabled : std::string(it.tip); g_tipDisabled = dis; }
        }
        return h;
    };
    auto label = [&](float yy) {
        std::string s = it.label; if (it.reload) s += " (Needs reload)";
        text(s.c_str(), x, yy, 18, withA(WHITE, alpha));
        return L(26);
    };
    switch (it.t)
    {
    case T_HEADER:
    {
        const Vector2 m = measure(it.label, 22);
        text(it.label, x, y, 22, WHITE);
        DrawRectangle((int)(x + m.x + L(12)), (int)(y + m.y * 0.55f), (int)(w - m.x - L(12)), std::max(1, (int)L(2)), withA(WHITE, 0.35f));
        return m.y + gap;
    }
    case T_TEXT:
    {
        text(it.label, x, y, 17, withA(WHITE, 0.85f));
        return L(26);
    }
    case T_CHECK:
    case T_FLAG:
    {
        const Rectangle r{x, y, w, H};
        bool on;
        if (it.t == T_FLAG) on = (g_svr06Flags.load() & (1 << (int)it.def)) != 0; else on = ps2settings::getInt(it.key, (int)it.def) != 0;
        const bool h = hov(r);
        const Rectangle box{x, y, H, H};
        rrect(box, 3, withA(on ? g_pal.on : g_pal.main, alpha));
        DrawRectangleRoundedLines(box, std::min(1.0f, L(3) / (H * 0.5f)), 6, withA(BLACK, 0.3f * alpha));
        if (h && !dis) rrect(box, 3, withA(WHITE, 0.10f));
        if (on)
        {
            const float s = H / 6.0f;
            Vector2 a{x + s * 1.4f, y + H * 0.52f}, b{x + s * 2.6f, y + H * 0.70f}, c2{x + s * 4.7f, y + H * 0.30f};
            DrawLineEx(a, b, std::max(2.0f, L(3)), withA(WHITE, 0.8f * alpha)); DrawLineEx(b, c2, std::max(2.0f, L(3)), withA(WHITE, 0.8f * alpha));
        }
        std::string s = it.label; text(s.c_str(), x + H + L(10), y + (H - measure("Ag", 18).y) * 0.5f, 18, withA(WHITE, alpha));
        if (h && !dis && C.click)
        {
            if (it.t == T_FLAG) { if (on) g_svr06Flags.fetch_and(~(1 << (int)it.def)); else g_svr06Flags.fetch_or(1 << (int)it.def); }
            else ps2settings::set(it.key, on ? 0 : 1);
        }
        return H + gap;
    }
    case T_SLIDER:
    {
        const float ly = y; const float lh = label(ly);
        const Rectangle r{x, ly + lh, w, H};
        const bool h = hov(r);
        double v = ps2settings::get(it.key, it.def);
        v = std::max(it.lo, std::min(it.hi, v));
        rrect(r, 3, withA(g_pal.dark, alpha));
        const float t = (float)((v - it.lo) / std::max(1e-9, it.hi - it.lo));
        const float gw = L(18), gx = r.x + L(4) + (r.width - gw - L(8)) * t;
        rrect(Rectangle{gx, r.y + L(4), gw, H - L(8)}, 3, withA(WHITE, (h ? 0.5f : 0.4f) * alpha));
        if (it.unit && it.unit[0] == '%') std::snprintf(buf, sizeof buf, "%d%%", (int)std::lround(v));
        else if (it.unit && it.unit[0] == 'x') std::snprintf(buf, sizeof buf, "%dx", (int)std::lround(v));
        else std::snprintf(buf, sizeof buf, "%d%s", (int)std::lround(v), it.unit ? it.unit : "");
        textC(buf, r, 18, withA(WHITE, alpha));
        if (!dis)
        {
            if (h && C.click) g_dragSlider = idx;
            if (g_dragSlider == idx)
            {
                if (C.down)
                {
                    const float tt = std::max(0.0f, std::min(1.0f, (C.mp.x - r.x - gw * 0.5f - L(4)) / std::max(1.0f, r.width - gw - L(8))));
                    const double nv = std::round(it.lo + (it.hi - it.lo) * tt);
                    if (nv != std::round(v)) ps2settings::set(it.key, nv);
                }
                else g_dragSlider = -1;
            }
            if (h && C.rclick) ps2settings::reset(it.key);
        }
        return lh + H + gap;
    }
    case T_COMBO:
    {
        const float lh = label(y);
        const Rectangle r{x, y + lh, w, H};
        const bool h = hov(r);
        int v = ps2settings::getInt(it.key, (int)it.def); if (v < 0 || v >= it.nopts) v = (int)it.def;
        rrect(r, 3, withA(h && !dis ? g_pal.light : g_pal.dark, alpha));
        text(it.opts[v], r.x + L(10), r.y + (H - measure("Ag", 18).y) * 0.5f, 18, withA(WHITE, alpha));
        // arrow
        const float ax = r.x + r.width - L(24), ay = r.y + H * 0.5f;
        DrawTriangle(Vector2{ax - L(6), ay - L(3)}, Vector2{ax, ay + L(5)}, Vector2{ax + L(6), ay - L(3)}, withA(WHITE, 0.7f * alpha));
        if (h && !dis && C.click) g_openCombo = (g_openCombo == idx) ? -1 : idx;
        return lh + H + gap;
    }
    case T_BIND:
    {
        const float half = w * 0.42f;
        text(it.label, x, y + (H - measure("Ag", 18).y) * 0.5f, 18, withA(WHITE, alpha));
        const Rectangle r{x + half, y, w - half, H};
        const bool h = hov(r);
        const bool cap = g_bindCapture == idx;
        int v = ps2settings::getInt(it.key, (int)it.def); if (v < 0 || v > 17) v = 0;
        rrect(r, 3, withA(cap ? g_pal.on : (h && !dis ? g_pal.light : g_pal.dark), alpha));
        textC(cap ? "Press a button..." : kPadBtnName[v], r, 18, withA(WHITE, alpha));
        if (cap)
        {
            const int b = GetGamepadButtonPressed();
            if (b > 0 && b < 18 && GetTime() - g_bindCaptureT > 0.15) { ps2settings::set(it.key, b); g_bindCapture = -1; }
            else if (IsKeyPressed(KEY_ESCAPE) || GetTime() - g_bindCaptureT > 8.0) g_bindCapture = -1;
        }
        else if (h && !dis && C.click) { g_bindCapture = idx; g_bindCaptureT = GetTime(); }
        if (h && !dis && C.rclick) { ps2settings::reset(it.key); g_bindCapture = -1; }
        return H + L(6);
    }
    case T_BUTTON:
    {
        const Rectangle r{x, y, w, H};
        const bool h = hov(r);
        const bool danger = it.action == A_RESET_ALL;
        const bool conf = g_confirm == it.action && it.action != 0 && GetTime() - g_confirmT < 3.0;
        Color base = danger ? Color{200, 30, 30, 255} : g_pal.main;
        rrect(r, 3, withA(h && !dis ? lerpC(base, WHITE, 0.10f) : base, alpha));
        DrawRectangleRoundedLines(r, std::min(1.0f, L(3) / (H * 0.5f)), 6, withA(BLACK, 0.3f * alpha));
        textC(conf ? "Click again to confirm" : it.label, r, 18, withA(WHITE, alpha));
        if (h && !dis && C.click)
        {
            if (danger && !conf) { g_confirm = it.action; g_confirmT = GetTime(); }
            else { g_confirm = 0; doAction(it.action); }
        }
        return H + gap;
    }
    }
    return 0;
}

// icon buttons (power / restart / close)
void iconButton(Rectangle r, int kind, Color base, const char *tip, bool &clicked)
{
    const bool h = hit(r, C.mp);
    rrect(r, 3, h ? lerpC(base, WHITE, 0.15f) : base);
    DrawRectangleRoundedLines(r, std::min(1.0f, L(3) / (r.height * 0.5f)), 6, withA(BLACK, 0.3f));
    const Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float u = r.height * 0.26f, th = std::max(2.0f, L(3));
    const Color w = withA(WHITE, 0.9f);
    if (kind == 0)       // power
    {
        DrawRing(c, u * 0.82f, u * 0.82f + th, -50, 230, 24, w);
        DrawLineEx(Vector2{c.x, c.y - u * 1.0f}, Vector2{c.x, c.y - u * 0.1f}, th, w);
    }
    else if (kind == 1)  // restart
    {
        DrawRing(c, u * 0.9f, u * 0.9f + th, 40, 340, 24, w);
        DrawTriangle(Vector2{c.x + u * 1.5f, c.y - u * 0.15f}, Vector2{c.x + u * 0.3f, c.y - u * 0.9f}, Vector2{c.x + u * 0.5f, c.y + u * 0.35f}, w);
    }
    else                  // close
    {
        DrawLineEx(Vector2{c.x - u, c.y - u}, Vector2{c.x + u, c.y + u}, th, w);
        DrawLineEx(Vector2{c.x + u, c.y - u}, Vector2{c.x - u, c.y + u}, th, w);
    }
    if (h) { g_tipText = tip; g_tipDisabled = false; }
    clicked = h && C.click;
}

void drawTooltip()
{
    if (g_tipText.empty()) return;
    const float size = 17, padX = L(10), padY = L(8), maxW = L(380);
    // wrap
    std::vector<std::string> lines; std::string cur, word;
    auto flush = [&]() { if (!cur.empty()) lines.push_back(cur); cur.clear(); };
    for (size_t i = 0; i <= g_tipText.size(); ++i)
    {
        const char ch = i < g_tipText.size() ? g_tipText[i] : ' ';
        if (ch == ' ')
        {
            std::string t2 = cur.empty() ? word : cur + " " + word;
            if (measure(t2.c_str(), size).x > maxW && !cur.empty()) { flush(); cur = word; } else cur = t2;
            word.clear();
        }
        else word += ch;
    }
    flush();
    float w = 0; for (auto &l : lines) w = std::max(w, measure(l.c_str(), size).x);
    const float lh = measure("Ag", size).y + L(2);
    const float bw = w + padX * 2, bh = lines.size() * lh + padY * 2;
    float bx = C.mp.x + L(18), by = C.mp.y + L(18);
    if (bx + bw > GetScreenWidth()) bx = C.mp.x - bw - L(8);
    if (by + bh > GetScreenHeight()) by = C.mp.y - bh - L(8);
    rrect(Rectangle{bx, by, bw, bh}, 3, Color{12, 12, 14, 240});
    DrawRectangleRoundedLines(Rectangle{bx, by, bw, bh}, std::min(1.0f, L(3) / (bh * 0.5f)), 6, withA(WHITE, 0.25f));
    for (size_t i = 0; i < lines.size(); ++i) text(lines[i].c_str(), bx + padX, by + padY + i * lh, size, g_tipDisabled ? Color{255, 190, 120, 255} : WHITE);
}
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
    const bool wasOpen = g_svr06MenuOpen.load();
    bool open = wasOpen;
    if (IsKeyPressed(KEY_F1) && !g_searchFocus) open = !open;
    if (open && IsKeyPressed(KEY_ESCAPE)) { if (g_bindCapture >= 0) g_bindCapture = -1; else if (g_searchFocus) g_searchFocus = false; else open = false; }
    if (open && !wasOpen) { g_wasHidden = IsCursorHidden(); if (g_wasHidden) ShowCursor(); g_openCombo = -1; g_dragSlider = -1; }
    if (!open && wasOpen) { if (g_wasHidden) HideCursor(); g_searchFocus = false; g_confirm = 0; }
    g_svr06MenuOpen.store(open);
    if (!open) return;

    buildItems();
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
    applyPalette();

    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    const float userScale = std::max(0.5f, ps2settings::getFloat("menu.scale", 100) / 100.0f);
    g_S = std::min(W / 1920.0f, H / 1032.0f) * userScale;
    g_ox = (W - 1920.0f * g_S) * 0.5f; g_oy = (H - 1032.0f * g_S) * 0.5f;

    C.mp = GetMousePosition();
    C.click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    C.down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    C.rclick = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    g_tipText.clear();
    if (g_confirm && GetTime() - g_confirmT > 3.0) g_confirm = 0;

    // gamepad: LB/RB switch tab, dpad up/down sidebar
    for (int gi = 0; gi < 4; ++gi)
    {
        if (!IsGamepadAvailable(gi)) continue;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) g_tab = (g_tab + 3) % 4;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) g_tab = (g_tab + 1) % 4;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_UP)) g_side[g_tab] = (g_side[g_tab] + kSideN[g_tab] - 1) % kSideN[g_tab];
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) g_side[g_tab] = (g_side[g_tab] + 1) % kSideN[g_tab];
    }

    // ---- window
    const float opacity = std::max(0.0f, std::min(1.0f, ps2settings::getFloat("menu.opacity", 85) / 100.0f));
    const Rectangle panel{X(90), Y(40), L(1740), L(952)};
    rrect(panel, 4, Color{7, 7, 6, (unsigned char)(opacity * 255)});
    DrawRectangleRoundedLines(panel, std::min(1.0f, L(4) / (panel.height * 0.5f)), 6, withA(WHITE, 0.15f));

    bool clicked = false;
    // tabs
    {
        float tx = X(108);
        for (int i = 0; i < 4; ++i)
        {
            const Vector2 m = measure(kTabNames[i], 20);
            const Rectangle r{tx, Y(66), m.x + L(44), L(50)};
            const bool h = hit(r, C.mp);
            const bool act = (i == g_tab) && g_search.empty();
            rrect(r, 3, act ? g_pal.light : (h ? g_pal.light : g_pal.main));
            if (act) DrawRectangle((int)r.x + (int)L(8), (int)(r.y + r.height - L(4)), (int)(r.width - L(16)), std::max(1, (int)L(3)), withA(WHITE, 0.9f));
            textC(kTabNames[i], r, 20, WHITE);
            if (h && C.click) { g_tab = i; g_search.clear(); g_openCombo = -1; }
            tx += r.width + L(10);
        }
    }
    // system buttons
    {
        const float s = L(50);
        Rectangle rClose{X(1816) - s, Y(66), s, s}, rRest{rClose.x - s - L(8), Y(66), s, s}, rPow{rRest.x - s - L(8), Y(66), s, s};
        const Color red{200, 30, 30, 255}, grey{115, 115, 115, 255};
        bool c1 = false, c2 = false, c3 = false;
        iconButton(rPow, 0, red, g_confirm == 1001 ? "Click again to quit the game" : "Quit the game", c1);
        iconButton(rRest, 1, red, g_confirm == 1002 ? "Click again to restart the game" : "Restart the game", c2);
        iconButton(rClose, 2, grey, "Close this menu (F1)", c3);
        if (c1) { if (g_confirm == 1001) std::_Exit(0); g_confirm = 1001; g_confirmT = GetTime(); }
        if (c2) { if (g_confirm == 1002) restartGame(); g_confirm = 1002; g_confirmT = GetTime(); }
        if (c3) { g_svr06MenuOpen.store(false); if (g_wasHidden) HideCursor(); g_searchFocus = false; clicked = true; }
        // search box
        const Rectangle sb{rPow.x - L(300) - L(14), Y(66), L(300), s};
        rrect(sb, 3, g_pal.search);
        DrawRectangleRoundedLines(sb, std::min(1.0f, L(3) / (s * 0.5f)), 6, g_searchFocus ? withA(WHITE, 0.6f) : withA(BLACK, 0.3f));
        if (C.click) g_searchFocus = hit(sb, C.mp);
        if (g_searchFocus)
        {
            int ch;
            while ((ch = GetCharPressed()) > 0) if (ch >= 32 && ch < 127 && g_search.size() < 40) g_search += (char)ch;
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !g_search.empty()) g_search.pop_back();
        }
        const bool empty = g_search.empty();
        std::string shown = empty ? "Search..." : g_search;
        if (g_searchFocus && ((int)(GetTime() * 2) & 1)) shown += "|";
        text(shown.c_str(), sb.x + L(14), sb.y + (s - measure("Ag", 18).y) * 0.5f, 18, withA(WHITE, empty && !g_searchFocus ? 0.4f : 0.95f));
    }
    // rule + divider
    DrawRectangle((int)X(103), (int)Y(129), (int)L(1713), std::max(2, (int)L(3)), WHITE);
    DrawRectangle((int)X(378), (int)Y(140), std::max(2, (int)L(2)), (int)L(832), WHITE);

    // sidebar
    if (g_search.empty())
    {
        float sy = Y(152);
        for (int i = 0; i < kSideN[g_tab]; ++i)
        {
            const Rectangle r{X(108), sy, L(262), L(46)};
            const bool h = hit(r, C.mp), sel = g_side[g_tab] == i;
            rrect(r, 3, sel || h ? g_pal.light : g_pal.main);
            textC(kSides[g_tab][i], r, 20, WHITE);
            if (h && C.click) { g_side[g_tab] = i; g_openCombo = -1; }
            sy += L(46) + L(8);
        }
    }
    else
    {
        text("Search results", X(130), Y(158), 20, withA(WHITE, 0.7f));
    }

    // content
    const Rectangle content{X(386), Y(140), L(1440), L(838)};
    C.view = content;
    C.inView = hit(content, C.mp) && g_openCombo < 0;
    const int sideIdx = g_search.empty() ? g_side[g_tab] : 7;
    float &scroll = g_scroll[g_tab][sideIdx];
    if (hit(content, C.mp)) scroll -= GetMouseWheelMove() * L(60);
    if (scroll < 0) scroll = 0;
    const float colW = L(464);
    const float cx[2] = {X(396), X(869)};
    float cy[2] = {content.y + L(8) - scroll, content.y + L(8) - scroll};
    BeginScissorMode((int)content.x, (int)content.y, (int)content.width, (int)content.height);
    int comboIdx = -1; Rectangle comboRect{};
    if (g_search.empty())
    {
        for (size_t i = 0; i < g_items.size(); ++i)
        {
            const Item &it = g_items[i];
            if (it.tab != g_tab || it.side != g_side[g_tab]) continue;
            const float before = cy[it.col];
            cy[it.col] += drawItem((int)i, it, cx[it.col], cy[it.col], colW);
            if (it.t == T_COMBO && g_openCombo == (int)i) { comboIdx = (int)i; comboRect = Rectangle{cx[it.col], before + L(26), colW, L(36)}; }
        }
    }
    else
    {
        int n = 0;
        for (size_t i = 0; i < g_items.size(); ++i)
        {
            const Item &it = g_items[i];
            if (it.t == T_HEADER || it.t == T_TEXT) continue;
            if (!ci_contains(it.label, g_search)) continue;
            const int col = n++ & 1;
            const float before = cy[col];
            cy[col] += drawItem((int)i, it, cx[col], cy[col], colW);
            if (it.t == T_COMBO && g_openCombo == (int)i) { comboIdx = (int)i; comboRect = Rectangle{cx[col], before + L(26), colW, L(36)}; }
        }
        if (n == 0) text("No matching options.", cx[0], cy[0], 18, withA(WHITE, 0.6f));
    }
    EndScissorMode();
    {
        const float total = std::max(cy[0], cy[1]) + scroll - content.y;
        const float maxS = std::max(0.0f, total - content.height + L(20));
        if (scroll > maxS) scroll = maxS;
        if (maxS > 0)
        {
            const float th = content.height * content.height / (content.height + maxS);
            const float ty = content.y + (content.height - th) * (scroll / maxS);
            rrect(Rectangle{content.x + content.width - L(6), ty, L(5), th}, 2, withA(WHITE, 0.3f));
        }
    }

    // open combo popup (drawn on top, unclipped)
    if (comboIdx >= 0)
    {
        const Item &it = g_items[comboIdx];
        const float oh = L(36);
        const Rectangle pr{comboRect.x, comboRect.y + comboRect.height + L(2), comboRect.width, oh * it.nopts};
        rrect(Rectangle{pr.x - 1, pr.y - 1, pr.width + 2, pr.height + 2}, 3, withA(BLACK, 0.5f));
        const int cur = ps2settings::getInt(it.key, (int)it.def);
        for (int k = 0; k < it.nopts; ++k)
        {
            const Rectangle r{pr.x, pr.y + k * oh, pr.width, oh};
            const bool h = hit(r, C.mp);
            DrawRectangleRec(r, h ? g_pal.light : g_pal.dark);
            text(it.opts[k], r.x + L(10), r.y + (oh - measure("Ag", 18).y) * 0.5f, 18, k == cur ? Color{255, 255, 255, 255} : withA(WHITE, 0.8f));
            if (h && C.click) { ps2settings::set(it.key, k); g_openCombo = -2; }
        }
        if (g_openCombo != -2 && C.click && !hit(pr, C.mp) && !hit(comboRect, C.mp)) g_openCombo = -2;
    }
    if (g_openCombo == -2) g_openCombo = -1;

    drawTooltip();
    (void)clicked;
}
