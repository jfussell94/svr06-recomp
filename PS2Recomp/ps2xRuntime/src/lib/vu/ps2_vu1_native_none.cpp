// CL-0271: empty native VU1 program table, used when ps2_vu1_native_gen.cpp (generated locally from the game's VU1 microcode by
// svr06-recomp/tools/vu1bench/gen.cpp, never published) is absent. All VU1 programs then run in the interpreter.
#include "ps2_vu1_native.h"
extern const Vu1NativeProg g_vu1NativeProgs[] = {{0u, 0u, nullptr, nullptr, nullptr}};
extern const uint32_t g_vu1NativeProgCount = 0u;
extern const Vu1NativeProg g_vu0NativeProgs[] = {{0u, 0u, nullptr, nullptr, nullptr}};
extern const uint32_t g_vu0NativeProgCount = 0u;
