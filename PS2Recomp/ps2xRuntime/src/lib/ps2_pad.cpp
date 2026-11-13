#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include "runtime/ps2_pad.h"
#include "ps2_host_backend.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include "ps2_settings.h"

std::atomic<int> g_svr06ForceBtn{0};
void svr06RevTick(int player, uint16_t &btns);
std::atomic<uint64_t> g_svr06PadView[2] = {{0xFFFFull}, {0xFFFFull}}; // what the game would see per player: low 16 bits = buttons (active low), then LX LY RX RY bytes (input viewer in the F1 menu)
// SvR06: a real START press that skipped the title movie is carried over to the game's own PRESS START prompt (one physical press = one menu step).
std::atomic<long long> g_svr06StartCarryNs{0};
std::atomic<bool> g_svr06StartDown{false}; // true while the pad data handed to the game has START pressed // bit mask of PS2 pad buttons held by test hooks (0x0008 = START)

namespace
{
    constexpr uint8_t kPadAnalogMarker = 0x73;
    constexpr uint8_t kPadStickCenter = 0x80;

    constexpr uint16_t PAD_LEFT = 0x0080u;
    constexpr uint16_t PAD_DOWN = 0x0040u;
    constexpr uint16_t PAD_RIGHT = 0x0020u;
    constexpr uint16_t PAD_UP = 0x0010u;
    constexpr uint16_t PAD_START = 0x0008u;
    constexpr uint16_t PAD_R3 = 0x0004u;
    constexpr uint16_t PAD_L3 = 0x0002u;
    constexpr uint16_t PAD_SELECT = 0x0001u;
    constexpr uint16_t PAD_SQUARE = 0x8000u;
    constexpr uint16_t PAD_CROSS = 0x4000u;
    constexpr uint16_t PAD_CIRCLE = 0x2000u;
    constexpr uint16_t PAD_TRIANGLE = 0x1000u;
    constexpr uint16_t PAD_R1 = 0x0800u;
    constexpr uint16_t PAD_L1 = 0x0400u;
    constexpr uint16_t PAD_R2 = 0x0200u;
    constexpr uint16_t PAD_L2 = 0x0100u;
}

// ---- Controller mapping (per player, PCSX2-style). Settings keys (svr06_settings.json, also edited in the F1 menu):
//   input.pN.device   0 = Auto (P1 = first connected gamepad, P2 = second), 1..4 = gamepad 1..4, 5 = none
//   input.pN.keyboard 1 = keyboard also drives this player (default P1 on, P2 off)
//   input.pN.btn.<name> raylib GamepadButton code bound to that PS2 button
//   input.pN.stickdpad 1 = left stick also presses the D-pad (default on);  controls.deadzone = stick deadzone in %
namespace svr06input
{
struct PadBind { const char *name; uint16_t mask; int defBtn; };
extern const PadBind kPadBinds[16];
const PadBind kPadBinds[16] = {
    {"up", PAD_UP, GAMEPAD_BUTTON_LEFT_FACE_UP},           {"down", PAD_DOWN, GAMEPAD_BUTTON_LEFT_FACE_DOWN},
    {"left", PAD_LEFT, GAMEPAD_BUTTON_LEFT_FACE_LEFT},     {"right", PAD_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT},
    {"cross", PAD_CROSS, GAMEPAD_BUTTON_RIGHT_FACE_DOWN},  {"circle", PAD_CIRCLE, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT},
    {"square", PAD_SQUARE, GAMEPAD_BUTTON_RIGHT_FACE_LEFT},{"triangle", PAD_TRIANGLE, GAMEPAD_BUTTON_RIGHT_FACE_UP},
    {"l1", PAD_L1, GAMEPAD_BUTTON_LEFT_TRIGGER_1},         {"r1", PAD_R1, GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
    {"l2", PAD_L2, GAMEPAD_BUTTON_LEFT_TRIGGER_2},         {"r2", PAD_R2, GAMEPAD_BUTTON_RIGHT_TRIGGER_2},
    {"l3", PAD_L3, GAMEPAD_BUTTON_LEFT_THUMB},             {"r3", PAD_R3, GAMEPAD_BUTTON_RIGHT_THUMB},
    {"select", PAD_SELECT, GAMEPAD_BUTTON_MIDDLE_LEFT},    {"start", PAD_START, GAMEPAD_BUTTON_MIDDLE_RIGHT}};
int bindingFor(int player, int i)
{
    char k[64]; std::snprintf(k, sizeof k, "input.p%d.btn.%s", player + 1, kPadBinds[i].name);
    return ps2settings::getInt(k, kPadBinds[i].defBtn);
}
int defaultKey(int i, int alt)
{
    static const int kDef[16][2] = {{KEY_UP, KEY_W}, {KEY_DOWN, KEY_S}, {KEY_LEFT, KEY_A}, {KEY_RIGHT, KEY_D}, {KEY_X, KEY_SPACE}, {KEY_C, KEY_ESCAPE}, {KEY_Z, KEY_KP_0}, {KEY_V, KEY_KP_1},
                                    {KEY_Q, 0}, {KEY_E, 0}, {KEY_LEFT_SHIFT, 0}, {KEY_RIGHT_SHIFT, 0}, {0, 0}, {0, 0}, {KEY_TAB, 0}, {KEY_ENTER, 0}};
    return kDef[i & 15][alt ? 1 : 0];
}
int keyFor(int player, int i, int alt)
{
    char k[72]; std::snprintf(k, sizeof k, "input.p%d.key%s.%s", player + 1, alt ? "2" : "", kPadBinds[i].name);
    return ps2settings::getInt(k, defaultKey(i, alt));
}
// Which raylib gamepad drives this player (-1 = none).
int gamepadFor(int player)
{
    char k[32]; std::snprintf(k, sizeof k, "input.p%d.device", player + 1);
    const int dev = ps2settings::getInt(k, 0);
    if (dev >= 1 && dev <= 4) return IsGamepadAvailable(dev - 1) ? dev - 1 : -1;
    if (dev != 0) return -1;
    // Auto: skip gamepads explicitly assigned to the other player, then take the player's N-th free pad.
    char ko[32]; std::snprintf(ko, sizeof ko, "input.p%d.device", 2 - player);
    const int other = ps2settings::getInt(ko, 0);
    int n = 0;
    for (int gi = 0; gi < 4; ++gi)
    {
        if (!IsGamepadAvailable(gi) || (other >= 1 && other <= 4 && other - 1 == gi)) continue;
        if (n == player || (other != 0)) return gi;
        ++n;
    }
    return -1;
}
bool keyboardFor(int player)
{
    char k[32]; std::snprintf(k, sizeof k, "input.p%d.keyboard", player + 1);
    return ps2settings::getInt(k, player == 0 ? 1 : 0) != 0;
}
} // namespace svr06input

bool PSPadBackend::readState(int port, int slot, uint8_t *data, size_t size)
{
    if (!data || size < 32)
        return false;

    std::memset(data, 0, 32);
    data[0] = 0x01;
    data[1] = kPadAnalogMarker;
    data[2] = 0xFF;
    data[3] = 0xFF;
    data[4] = data[5] = data[6] = data[7] = kPadStickCenter;
    // [padport] ISS-126: only pad 1 (port 0, slot 0) gets keyboard/gamepad input. Every other port/slot reports an idle pad,
    // otherwise the game sees several controllers pressing the same button (menu cursor skipped lines, extra players in Exhibition).
    // Controller mapping: port 0 = player 1, port 1 = player 2 (slot 0 only; multitap slots stay idle).
    if ((port != 0 && port != 1) || slot != 0)
        return true;
    const int player = port;

    uint16_t btns = 0xFFFFu;
    const int kGamepad = svr06input::gamepadFor(player);
    const bool useGamepad = kGamepad >= 0;
    {
        static int s_logged[2] = {-2, -2};
        if (s_logged[player] != kGamepad) { s_logged[player] = kGamepad; if (useGamepad) std::fprintf(stderr, "[pad] player %d -> gamepad %d: %s\n", player + 1, kGamepad, GetGamepadName(kGamepad)); else std::fprintf(stderr, "[pad] player %d -> no gamepad\n", player + 1); }
    }
    auto clearBit = [&btns](uint16_t mask)
    { btns &= ~mask; };

    if (useGamepad)
    {
        for (int i = 0; i < 16; ++i)
        {
            const int b = svr06input::bindingFor(player, i);
            if (b > 0 && IsGamepadButtonDown(kGamepad, b)) clearBit(svr06input::kPadBinds[i].mask);
        }
        const float dz = std::min(0.9f, std::max(0.0f, ps2settings::getFloat("controls.deadzone", 15.0f) / 100.0f));
        auto dzf = [dz](float v) { const float a = std::fabs(v); if (a <= dz) return 0.0f; const float t = (a - dz) / (1.0f - dz); return v < 0 ? -t : t; };
        float lx = dzf(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_X));
        float ly = dzf(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_LEFT_Y));
        float rx = dzf(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_X));
        float ry = dzf(GetGamepadAxisMovement(kGamepad, GAMEPAD_AXIS_RIGHT_Y));
        char sk[32]; std::snprintf(sk, sizeof sk, "input.p%d.stickdpad", player + 1);
        if (ps2settings::getInt(sk, 1) != 0)
        {
            if (lx < -0.5f) clearBit(PAD_LEFT);
            if (lx > 0.5f) clearBit(PAD_RIGHT);
            if (ly < -0.5f) clearBit(PAD_UP);
            if (ly > 0.5f) clearBit(PAD_DOWN);
        }
        {
            char k1[40], k2[40]; std::snprintf(k1, sizeof k1, "input.p%d.swapsticks", player + 1); std::snprintf(k2, sizeof k2, "input.p%d.invertry", player + 1);
            if (ps2settings::getInt(k1, 0)) { std::swap(lx, rx); std::swap(ly, ry); }
            if (ps2settings::getInt(k2, 0)) ry = -ry;
            std::snprintf(k2, sizeof k2, "input.p%d.invertly", player + 1);
            if (ps2settings::getInt(k2, 0)) ly = -ly;
        }
        data[6] = static_cast<uint8_t>(128 + lx * 127);
        data[7] = static_cast<uint8_t>(128 + ly * 127);
        data[4] = static_cast<uint8_t>(128 + rx * 127);
        data[5] = static_cast<uint8_t>(128 + ry * 127);
    }
    if (svr06input::keyboardFor(player))
    {
        for (int i = 0; i < 16; ++i)
            for (int alt = 0; alt < 2; ++alt)
            {
                const int key = svr06input::keyFor(player, i, alt);
                if (key > 0 && IsKeyDown(key)) clearBit(svr06input::kPadBinds[i].mask);
            }
    }
    {   // accessibility: mash assist (hold Cross to pulse it) and sticky shoulders
        char k[40];
        std::snprintf(k, sizeof k, "input.p%d.mashAssist", player + 1);
        if (ps2settings::getInt(k, 0) && !(btns & PAD_CROSS))
        {
            const double hz = std::max(2.0, (double)ps2settings::getFloat("input.mashHz", 12.0f));
            if (std::fmod(GetTime() * hz, 1.0) < 0.5) btns |= PAD_CROSS; // off half of each cycle
        }
        std::snprintf(k, sizeof k, "input.p%d.sticky", player + 1);
        if (ps2settings::getInt(k, 0))
        {
            static uint16_t s_latched[2] = {0, 0}, s_prev[2] = {0, 0};
            const uint16_t sh = PAD_L1 | PAD_R1 | PAD_L2 | PAD_R2;
            const uint16_t down = (uint16_t)(~btns & sh), edge = down & ~s_prev[player];
            s_prev[player] = down; s_latched[player] ^= edge;
            btns |= sh; btns &= ~s_latched[player];
        }
    }
    svr06RevTick(player, btns);
    g_svr06PadView[player].store((uint64_t)btns | ((uint64_t)data[6] << 16) | ((uint64_t)data[7] << 24) | ((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40));
    { extern std::atomic<bool> g_svr06MenuOpen; if (g_svr06MenuOpen.load()) { data[2] = data[3] = 0xFF; data[4] = data[5] = data[6] = data[7] = kPadStickCenter; return true; } } // menu open: game sees an idle pad

    if (player != 0)
    {
        data[2] = static_cast<uint8_t>(btns & 0xFF);
        data[3] = static_cast<uint8_t>(btns >> 8);
        return true;
    }
    static std::atomic<bool> s_realInput{false};
    if (btns != 0xFFFFu && !s_realInput.exchange(true))
        std::fprintf(stderr, "[pad] real input detected - test automation disabled\n");

    {
        const int force = s_realInput.load() ? 0 : g_svr06ForceBtn.load();
        if (force & PAD_START)
        {
            static int s_fs = 0;
            if (s_fs++ < 24) std::fprintf(stderr, "[padforce] START seen by readState port=%d (call %d)\n", 0, s_fs);
            clearBit(PAD_START);
        }
        {
            static std::atomic<long long> s_reads{0};
            const long long n = ++s_reads;
            if ((n % 600) == 1) std::fprintf(stderr, "[padforce] readState calls=%lld\n", n);
        }
        if (force & PAD_CROSS) clearBit(PAD_CROSS);
    }
    {
        const long long carryAt = g_svr06StartCarryNs.load();
        if (carryAt != 0)
        {
            const long long nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            if (nowNs >= carryAt + 250000000LL) g_svr06StartCarryNs.store(0);
            else if (nowNs >= carryAt) clearBit(PAD_START);
        }
    }
    { extern std::atomic<int> g_svr06CmdBtn; btns &= static_cast<uint16_t>(~g_svr06CmdBtn.load()); }
    g_svr06StartDown.store((btns & PAD_START) == 0);
    data[2] = static_cast<uint8_t>(btns & 0xFF);
    data[3] = static_cast<uint8_t>(btns >> 8);
    return true;
}
