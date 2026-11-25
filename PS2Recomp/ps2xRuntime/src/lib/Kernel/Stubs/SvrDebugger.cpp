// SvR06 in-game debugger (CL-0390). Function breakpoints, R5900 disassembly, guest call stacks, memory search and
// memory watchpoints for the native (statically recompiled) build, driven from the F1 Runtime Debugger "Debugger" tab
// and from the live console (analysis/live/cmd.txt, tools/svrlive.py).
//
// Limits compared with PCSX2's debugger: the game runs as native code, so there is no single-instruction stepping and
// a breakpoint can only sit on a function entry (every recompiled function and every overlay function that the runtime
// dispatches). Memory watchpoints use the existing hardware/page watch (ps2_pagewatch.cpp, max 4 hardware slots).
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

void svr06PageWatchSet(uint8_t *rdram, uint32_t addr, uint32_t len);
void svr06HwWatchRwMode(int readsToo);

namespace svrdbg
{
namespace
{
std::atomic<PS2Runtime *> g_rt{nullptr};
std::atomic<uint8_t *> g_ram{nullptr};

const char *kReg[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                        "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};

uint32_t rd32(uint32_t a)
{
    uint8_t *r = g_ram.load();
    uint32_t v = 0;
    a &= 0x1FFFFFFu;
    if (r && a <= 0x2000000u - 4u) std::memcpy(&v, r + a, 4);
    return v;
}

std::string hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%X", v);
    return b;
}

// ---------------------------------------------------------------- disassembler
std::string fmt(const char *f, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(b, sizeof b, f, ap);
    va_end(ap);
    return b;
}

const char *mmi0(uint32_t s)
{
    static const char *t[32] = {"paddw", "psubw", "pcgtw", "pmaxw", "paddh", "psubh", "pcgth", "pmaxh", "paddb", "psubb", "pcgtb", nullptr, nullptr, nullptr, nullptr, nullptr,
                                "paddsw", "psubsw", "pextlw", "ppacw", "paddsh", "psubsh", "pextlh", "ppach", "paddsb", "psubsb", "pextlb", "ppacb", nullptr, nullptr, "pext5", "ppac5"};
    return t[s & 31];
}
const char *mmi1(uint32_t s)
{
    static const char *t[32] = {nullptr, "pabsw", "pceqw", "pminw", "padsbh", "pabsh", "pceqh", "pminh", nullptr, nullptr, "pceqb", nullptr, nullptr, nullptr, nullptr, nullptr,
                                "padduw", "psubuw", "pextuw", nullptr, "padduh", "psubuh", "pextuh", nullptr, "paddub", "psubub", "pextub", "qfsrv", nullptr, nullptr, nullptr, nullptr};
    return t[s & 31];
}
const char *mmi2(uint32_t s)
{
    static const char *t[32] = {"pmaddw", nullptr, "psllvw", "psrlvw", "pmsubw", nullptr, nullptr, nullptr, "pmfhi", "pmflo", "pinth", nullptr, "pmultw", "pdivw", "pcpyld", nullptr,
                                "pmaddh", "phmadh", "pand", "pxor", "pmsubh", "phmsbh", nullptr, nullptr, nullptr, nullptr, "pexeh", "prevh", "pmulth", "pdivbw", "pexew", "prot3w"};
    return t[s & 31];
}
const char *mmi3(uint32_t s)
{
    static const char *t[32] = {"pmadduw", nullptr, nullptr, "psravw", nullptr, nullptr, nullptr, nullptr, "pmthi", "pmtlo", "pinteh", nullptr, "pmultuw", "pdivuw", "pcpyud", nullptr,
                                nullptr, nullptr, "por", "pnor", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "pexch", "pcpyh", nullptr, nullptr, "pexcw", nullptr};
    return t[s & 31];
}
} // namespace

std::string disasm(uint32_t pc, uint32_t w)
{
    const uint32_t op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31, rd = (w >> 11) & 31, sa = (w >> 6) & 31, fn = w & 63;
    const int32_t simm = static_cast<int16_t>(w & 0xFFFF);
    const uint32_t uimm = w & 0xFFFF;
    const uint32_t btgt = pc + 4u + static_cast<uint32_t>(simm << 2);
    const char *S = kReg[rs], *T = kReg[rt], *D = kReg[rd];
    if (w == 0) return "nop";
    switch (op)
    {
    case 0:
    {
        static const char *sh[] = {"sll", nullptr, "srl", "sra"};
        if (fn <= 3 && sh[fn]) return fmt("%s %s, %s, %u", sh[fn], D, T, sa);
        switch (fn)
        {
        case 4: return fmt("sllv %s, %s, %s", D, T, S);
        case 6: return fmt("srlv %s, %s, %s", D, T, S);
        case 7: return fmt("srav %s, %s, %s", D, T, S);
        case 8: return fmt("jr %s", S);
        case 9: return rd == 31 ? fmt("jalr %s", S) : fmt("jalr %s, %s", D, S);
        case 10: return fmt("movz %s, %s, %s", D, S, T);
        case 11: return fmt("movn %s, %s, %s", D, S, T);
        case 12: return fmt("syscall 0x%X", (w >> 6) & 0xFFFFF);
        case 13: return fmt("break 0x%X", (w >> 6) & 0xFFFFF);
        case 15: return "sync";
        case 16: return fmt("mfhi %s", D);
        case 17: return fmt("mthi %s", S);
        case 18: return fmt("mflo %s", D);
        case 19: return fmt("mtlo %s", S);
        case 20: return fmt("dsllv %s, %s, %s", D, T, S);
        case 22: return fmt("dsrlv %s, %s, %s", D, T, S);
        case 23: return fmt("dsrav %s, %s, %s", D, T, S);
        case 24: return rd ? fmt("mult %s, %s, %s", D, S, T) : fmt("mult %s, %s", S, T);
        case 25: return rd ? fmt("multu %s, %s, %s", D, S, T) : fmt("multu %s, %s", S, T);
        case 26: return fmt("div %s, %s", S, T);
        case 27: return fmt("divu %s, %s", S, T);
        case 40: return fmt("mfsa %s", D);
        case 41: return fmt("mtsa %s", S);
        case 52: return fmt("teq %s, %s", S, T);
        case 56: return fmt("dsll %s, %s, %u", D, T, sa);
        case 58: return fmt("dsrl %s, %s, %u", D, T, sa);
        case 59: return fmt("dsra %s, %s, %u", D, T, sa);
        case 60: return fmt("dsll32 %s, %s, %u", D, T, sa);
        case 62: return fmt("dsrl32 %s, %s, %u", D, T, sa);
        case 63: return fmt("dsra32 %s, %s, %u", D, T, sa);
        default: break;
        }
        static const char *ar[64] = {};
        const char *n = nullptr;
        switch (fn)
        {
        case 32: n = "add"; break;
        case 33: n = "addu"; break;
        case 34: n = "sub"; break;
        case 35: n = "subu"; break;
        case 36: n = "and"; break;
        case 37: n = "or"; break;
        case 38: n = "xor"; break;
        case 39: n = "nor"; break;
        case 42: n = "slt"; break;
        case 43: n = "sltu"; break;
        case 44: n = "dadd"; break;
        case 45: n = "daddu"; break;
        case 46: n = "dsub"; break;
        case 47: n = "dsubu"; break;
        default: break;
        }
        (void)ar;
        if (n)
        {
            if (fn == 37 && rt == 0) return fmt("move %s, %s", D, S);
            if (fn == 45 && rt == 0) return fmt("move %s, %s", D, S);
            return fmt("%s %s, %s, %s", n, D, S, T);
        }
        return fmt("special.%u 0x%08X", fn, w);
    }
    case 1:
    {
        static const char *t[32] = {"bltz", "bgez", "bltzl", "bgezl", nullptr, nullptr, nullptr, nullptr, "tgei", "tgeiu", "tlti", "tltiu", "teqi", nullptr, "tnei", nullptr,
                                    "bltzal", "bgezal", "bltzall", "bgezall", nullptr, nullptr, nullptr, nullptr, "mtsab", "mtsah", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        if (rt == 24 || rt == 25) return fmt("%s %s, 0x%X", t[rt], S, uimm);
        if (t[rt] && rt < 8) return fmt("%s %s, 0x%X", t[rt], S, btgt);
        if (t[rt] && rt >= 16) return fmt("%s %s, 0x%X", t[rt], S, btgt);
        if (t[rt]) return fmt("%s %s, %d", t[rt], S, simm);
        return fmt("regimm 0x%08X", w);
    }
    case 2: return fmt("j 0x%X", (pc & 0xF0000000u) | ((w & 0x3FFFFFFu) << 2));
    case 3: return fmt("jal 0x%X", (pc & 0xF0000000u) | ((w & 0x3FFFFFFu) << 2));
    case 4: return rs == 0 && rt == 0 ? fmt("b 0x%X", btgt) : (rt == 0 ? fmt("beqz %s, 0x%X", S, btgt) : fmt("beq %s, %s, 0x%X", S, T, btgt));
    case 5: return rt == 0 ? fmt("bnez %s, 0x%X", S, btgt) : fmt("bne %s, %s, 0x%X", S, T, btgt);
    case 6: return fmt("blez %s, 0x%X", S, btgt);
    case 7: return fmt("bgtz %s, 0x%X", S, btgt);
    case 8: return fmt("addi %s, %s, %d", T, S, simm);
    case 9: return rs == 0 ? fmt("li %s, %d", T, simm) : fmt("addiu %s, %s, %d", T, S, simm);
    case 10: return fmt("slti %s, %s, %d", T, S, simm);
    case 11: return fmt("sltiu %s, %s, %d", T, S, simm);
    case 12: return fmt("andi %s, %s, 0x%X", T, S, uimm);
    case 13: return rs == 0 ? fmt("li %s, 0x%X", T, uimm) : fmt("ori %s, %s, 0x%X", T, S, uimm);
    case 14: return fmt("xori %s, %s, 0x%X", T, S, uimm);
    case 15: return fmt("lui %s, 0x%X", T, uimm);
    case 16:
        if (rs == 0) return fmt("mfc0 %s, $%u", T, rd);
        if (rs == 4) return fmt("mtc0 %s, $%u", T, rd);
        if (rs == 16 && fn == 24) return "eret";
        if (rs == 16 && fn == 56) return "ei";
        if (rs == 16 && fn == 57) return "di";
        return fmt("cop0 0x%08X", w);
    case 17:
    {
        const uint32_t ft = rt, fs = rd, fd = sa;
        if (rs == 0) return fmt("mfc1 %s, $f%u", T, fs);
        if (rs == 2) return fmt("cfc1 %s, $fcr%u", T, fs);
        if (rs == 4) return fmt("mtc1 %s, $f%u", T, fs);
        if (rs == 6) return fmt("ctc1 %s, $fcr%u", T, fs);
        if (rs == 8)
        {
            static const char *b[4] = {"bc1f", "bc1t", "bc1fl", "bc1tl"};
            return fmt("%s 0x%X", b[rt & 3], btgt);
        }
        if (rs == 16)
        {
            switch (fn)
            {
            case 0: return fmt("add.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 1: return fmt("sub.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 2: return fmt("mul.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 3: return fmt("div.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 4: return fmt("sqrt.s $f%u, $f%u", fd, ft);
            case 5: return fmt("abs.s $f%u, $f%u", fd, fs);
            case 6: return fmt("mov.s $f%u, $f%u", fd, fs);
            case 7: return fmt("neg.s $f%u, $f%u", fd, fs);
            case 22: return fmt("rsqrt.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 24: return fmt("adda.s $f%u, $f%u", fs, ft);
            case 25: return fmt("suba.s $f%u, $f%u", fs, ft);
            case 26: return fmt("mula.s $f%u, $f%u", fs, ft);
            case 28: return fmt("madd.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 29: return fmt("msub.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 30: return fmt("madda.s $f%u, $f%u", fs, ft);
            case 31: return fmt("msuba.s $f%u, $f%u", fs, ft);
            case 36: return fmt("cvt.w.s $f%u, $f%u", fd, fs);
            case 40: return fmt("max.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 41: return fmt("min.s $f%u, $f%u, $f%u", fd, fs, ft);
            case 48: return fmt("c.f.s $f%u, $f%u", fs, ft);
            case 50: return fmt("c.eq.s $f%u, $f%u", fs, ft);
            case 52: return fmt("c.lt.s $f%u, $f%u", fs, ft);
            case 54: return fmt("c.le.s $f%u, $f%u", fs, ft);
            default: break;
            }
        }
        if (rs == 20 && fn == 32) return fmt("cvt.s.w $f%u, $f%u", fd, fs);
        return fmt("cop1 0x%08X", w);
    }
    case 18:
        if (rs == 1) return fmt("qmfc2 %s, $vf%u", T, rd);
        if (rs == 2) return fmt("cfc2 %s, $vi%u", T, rd);
        if (rs == 5) return fmt("qmtc2 %s, $vf%u", T, rd);
        if (rs == 6) return fmt("ctc2 %s, $vi%u", T, rd);
        if (rs == 8) return fmt("bc2%s%s 0x%X", (rt & 1) ? "t" : "f", (rt & 2) ? "l" : "", btgt);
        return fmt("vu0 0x%08X", w);
    case 20: return fmt("beql %s, %s, 0x%X", S, T, btgt);
    case 21: return fmt("bnel %s, %s, 0x%X", S, T, btgt);
    case 22: return fmt("blezl %s, 0x%X", S, btgt);
    case 23: return fmt("bgtzl %s, 0x%X", S, btgt);
    case 24: return fmt("daddi %s, %s, %d", T, S, simm);
    case 25: return fmt("daddiu %s, %s, %d", T, S, simm);
    case 28:
    {
        const char *n = nullptr;
        switch (fn)
        {
        case 0: return fmt("madd %s, %s, %s", D, S, T);
        case 1: return fmt("maddu %s, %s, %s", D, S, T);
        case 4: return fmt("plzcw %s, %s", D, S);
        case 8: n = mmi0(sa); break;
        case 40: n = mmi1(sa); break;
        case 9: n = mmi2(sa); break;
        case 41: n = mmi3(sa); break;
        case 16: return fmt("mfhi1 %s", D);
        case 17: return fmt("mthi1 %s", S);
        case 18: return fmt("mflo1 %s", D);
        case 19: return fmt("mtlo1 %s", S);
        case 24: return fmt("mult1 %s, %s, %s", D, S, T);
        case 25: return fmt("multu1 %s, %s, %s", D, S, T);
        case 26: return fmt("div1 %s, %s", S, T);
        case 27: return fmt("divu1 %s, %s", S, T);
        case 32: return fmt("madd1 %s, %s, %s", D, S, T);
        case 33: return fmt("maddu1 %s, %s, %s", D, S, T);
        case 48: return fmt("pmfhl.%u %s", sa, D);
        case 49: return fmt("pmthl.%u %s", sa, S);
        case 52: return fmt("psllh %s, %s, %u", D, T, sa);
        case 54: return fmt("psrlh %s, %s, %u", D, T, sa);
        case 55: return fmt("psrah %s, %s, %u", D, T, sa);
        case 60: return fmt("psllw %s, %s, %u", D, T, sa);
        case 62: return fmt("psrlw %s, %s, %u", D, T, sa);
        case 63: return fmt("psraw %s, %s, %u", D, T, sa);
        default: break;
        }
        if (n) return fmt("%s %s, %s, %s", n, D, S, T);
        return fmt("mmi 0x%08X", w);
    }
    case 26: return fmt("ldl %s, %d(%s)", T, simm, S);
    case 27: return fmt("ldr %s, %d(%s)", T, simm, S);
    case 30: return fmt("lq %s, %d(%s)", T, simm, S);
    case 31: return fmt("sq %s, %d(%s)", T, simm, S);
    case 32: return fmt("lb %s, %d(%s)", T, simm, S);
    case 33: return fmt("lh %s, %d(%s)", T, simm, S);
    case 34: return fmt("lwl %s, %d(%s)", T, simm, S);
    case 35: return fmt("lw %s, %d(%s)", T, simm, S);
    case 36: return fmt("lbu %s, %d(%s)", T, simm, S);
    case 37: return fmt("lhu %s, %d(%s)", T, simm, S);
    case 38: return fmt("lwr %s, %d(%s)", T, simm, S);
    case 39: return fmt("lwu %s, %d(%s)", T, simm, S);
    case 40: return fmt("sb %s, %d(%s)", T, simm, S);
    case 41: return fmt("sh %s, %d(%s)", T, simm, S);
    case 42: return fmt("swl %s, %d(%s)", T, simm, S);
    case 43: return fmt("sw %s, %d(%s)", T, simm, S);
    case 44: return fmt("sdl %s, %d(%s)", T, simm, S);
    case 45: return fmt("sdr %s, %d(%s)", T, simm, S);
    case 46: return fmt("swr %s, %d(%s)", T, simm, S);
    case 47: return fmt("cache 0x%X, %d(%s)", rt, simm, S);
    case 49: return fmt("lwc1 $f%u, %d(%s)", rt, simm, S);
    case 51: return fmt("pref %u, %d(%s)", rt, simm, S);
    case 54: return fmt("lqc2 $vf%u, %d(%s)", rt, simm, S);
    case 55: return fmt("ld %s, %d(%s)", T, simm, S);
    case 57: return fmt("swc1 $f%u, %d(%s)", rt, simm, S);
    case 62: return fmt("sqc2 $vf%u, %d(%s)", rt, simm, S);
    case 63: return fmt("sd %s, %d(%s)", T, simm, S);
    default: break;
    }
    return fmt(".word 0x%08X", w);
}

// ---------------------------------------------------------------- call stack (stack scan for return addresses)
std::vector<uint32_t> backtrace(uint32_t sp, int maxFrames)
{
    std::vector<uint32_t> out;
    if (sp < 0x1000u || sp >= 0x2000000u) return out;
    for (uint32_t q = sp; q < sp + 0x1000u && static_cast<int>(out.size()) < maxFrames; q += 4)
    {
        const uint32_t v = rd32(q);
        if (v < 0x100000u || v >= 0x2000000u || (v & 3u)) continue;
        const uint32_t ins = rd32(v - 8u);
        if ((ins >> 26) == 3u || (ins & 0xFC00003Fu) == 9u) out.push_back(v);
    }
    return out;
}

// ---------------------------------------------------------------- breakpoints
namespace
{
constexpr int kBpMax = 64;
enum BpMode : int { BpLog = 0, BpPause = 1, BpCount = 2 };
struct Bp
{
    std::atomic<uint32_t> addr{0};
    std::atomic<int> mode{BpLog};
    std::atomic<bool> enabled{false};
    std::atomic<bool> hasCond{false};
    std::atomic<uint32_t> condReg{4}, condVal{0};
    PS2Runtime::RecompiledFunction orig = nullptr;
    std::atomic<uint64_t> hits{0};
    std::atomic<uint32_t> lastRa{0}, lastA0{0}, lastA1{0}, lastA2{0}, lastA3{0}, lastV0{0};
};
std::array<Bp, kBpMax> g_bp;
std::mutex g_logMx;
std::deque<std::string> g_log;
std::atomic<int> g_pausedSlot{-1};
std::atomic<bool> g_resume{false};
std::atomic<R5900Context *> g_pausedCtx{nullptr};
std::atomic<uint32_t> g_logCap{2000};

void logLine(const std::string &s)
{
    {
        std::lock_guard<std::mutex> l(g_logMx);
        g_log.push_back(s);
        while (g_log.size() > 1000) g_log.pop_front();
    }
    static std::mutex fmx;
    std::lock_guard<std::mutex> l(fmx);
    std::error_code ec;
    std::filesystem::create_directories("analysis/live", ec);
    if (FILE *f = std::fopen("analysis/live/dbg.txt", "a")) { std::fprintf(f, "%s\n", s.c_str()); std::fclose(f); }
}

void onHit(int I, uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
{
    Bp &b = g_bp[I];
    PS2Runtime::RecompiledFunction orig = b.orig;
    if (!b.enabled.load())
    {
        if (orig) orig(rdram, ctx, rt);
        return;
    }
    if (b.hasCond.load() && GPR_U32(ctx, b.condReg.load() & 31u) != b.condVal.load())
    {
        orig(rdram, ctx, rt);
        return;
    }
    const uint32_t a0 = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6), a3 = GPR_U32(ctx, 7), ra = GPR_U32(ctx, 31), sp = GPR_U32(ctx, 29);
    const uint64_t n = ++b.hits;
    b.lastRa = ra; b.lastA0 = a0; b.lastA1 = a1; b.lastA2 = a2; b.lastA3 = a3;
    const int mode = b.mode.load();
    std::string bt;
    if (mode != BpCount || n <= 4)
        for (uint32_t r : backtrace(sp, 10)) bt += " " + hex(r);
    if (mode == BpPause)
    {
        logLine(fmt("[bp] PAUSED at %X #%llu ra=%X a0=%X a1=%X a2=%X a3=%X sp=%X bt:", b.addr.load(), (unsigned long long)n, ra, a0, a1, a2, a3, sp) + bt);
        g_pausedCtx.store(ctx);
        g_resume.store(false);
        g_pausedSlot.store(I);
        while (!g_resume.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        g_pausedSlot.store(-1);
        g_pausedCtx.store(nullptr);
    }
    orig(rdram, ctx, rt);
    const uint32_t v0 = GPR_U32(ctx, 2);
    b.lastV0 = v0;
    if (mode == BpLog || (mode == BpPause) || (mode == BpCount && (n <= 4 || (n & (n - 1)) == 0)))
    {
        if (n <= g_logCap.load() || (n % 600u) == 0u)
            logLine(fmt("[bp] %X #%llu ra=%X a0=%X a1=%X a2=%X a3=%X -> v0=%X bt:", b.addr.load(), (unsigned long long)n, ra, a0, a1, a2, a3, v0) + bt);
    }
}

template <int I>
void bpThunk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt) { onHit(I, rdram, ctx, rt); }

template <int... Is>
constexpr std::array<PS2Runtime::RecompiledFunction, sizeof...(Is)> makeThunks(std::integer_sequence<int, Is...>) { return {&bpThunk<Is>...}; }
const auto g_thunks = makeThunks(std::make_integer_sequence<int, kBpMax>{});
std::mutex g_bpMx;
} // namespace

std::string bpAdd(uint32_t addr, int mode, bool hasCond, uint32_t condReg, uint32_t condVal)
{
    PS2Runtime *rt = g_rt.load();
    if (!rt) return "debugger not attached yet (open the game window first)";
    std::lock_guard<std::mutex> l(g_bpMx);
    for (int i = 0; i < kBpMax; ++i)
        if (g_bp[i].addr.load() == addr && g_bp[i].orig)
        {
            g_bp[i].mode = mode; g_bp[i].hasCond = hasCond; g_bp[i].condReg = condReg; g_bp[i].condVal = condVal; g_bp[i].enabled = true;
            return fmt("bp %X updated (slot %d)", addr, i);
        }
    for (int i = 0; i < kBpMax; ++i)
    {
        if (g_bp[i].orig) continue;
        PS2Runtime::RecompiledFunction f = rt->lookupFunction(addr);
        if (!f) return fmt("no function starts at %X (breakpoints go on function entries)", addr);
        g_bp[i].orig = f;
        g_bp[i].addr = addr; g_bp[i].mode = mode; g_bp[i].hits = 0; g_bp[i].hasCond = hasCond; g_bp[i].condReg = condReg; g_bp[i].condVal = condVal;
        g_bp[i].enabled = true;
        if (!rt->replaceFunction(addr, g_thunks[i]))
        {
            g_bp[i].orig = nullptr; g_bp[i].addr = 0; g_bp[i].enabled = false;
            return fmt("could not hook %X", addr);
        }
        return fmt("bp %X set (slot %d, %s)", addr, i, mode == BpPause ? "pause" : (mode == BpCount ? "count" : "log"));
    }
    return "all breakpoint slots in use";
}

std::string bpDel(uint32_t addr)
{
    PS2Runtime *rt = g_rt.load();
    std::lock_guard<std::mutex> l(g_bpMx);
    for (int i = 0; i < kBpMax; ++i)
        if (g_bp[i].orig && (addr == 0 || g_bp[i].addr.load() == addr))
        {
            g_bp[i].enabled = false;
            if (rt) rt->replaceFunction(g_bp[i].addr.load(), g_bp[i].orig);
            g_bp[i].orig = nullptr;
            g_bp[i].addr = 0;
            if (addr) return fmt("bp %X removed", addr);
        }
    if (g_pausedSlot.load() >= 0) g_resume = true;
    return addr ? fmt("no bp at %X", addr) : "all breakpoints removed";
}

std::string bpList()
{
    std::ostringstream o;
    for (int i = 0; i < kBpMax; ++i)
        if (g_bp[i].orig)
            o << fmt("slot %2d %08X %-5s %s hits=%llu lastRa=%X a0=%X v0=%X%s\n", i, g_bp[i].addr.load(),
                     g_bp[i].mode == BpPause ? "pause" : (g_bp[i].mode == BpCount ? "count" : "log"), g_bp[i].enabled ? "on " : "off",
                     (unsigned long long)g_bp[i].hits.load(), g_bp[i].lastRa.load(), g_bp[i].lastA0.load(), g_bp[i].lastV0.load(),
                     g_pausedSlot.load() == i ? "  <== PAUSED" : "");
    const std::string s = o.str();
    return s.empty() ? "no breakpoints\n" : s;
}

bool isPaused() { return g_pausedSlot.load() >= 0; }
uint32_t pausedAddr() { const int i = g_pausedSlot.load(); return i >= 0 ? g_bp[i].addr.load() : 0u; }
uint32_t pausedSp() { R5900Context *c = g_pausedCtx.load(); return c ? GPR_U32(c, 29) : 0u; }
void resume() { g_resume = true; }

std::string regsText()
{
    R5900Context *c = g_pausedCtx.load();
    if (!c) return "not paused\n";
    std::ostringstream o;
    for (int r = 0; r < 32; ++r)
    {
        alignas(16) uint64_t q[2];
        std::memcpy(q, &c->r[r], 16);
        o << fmt("%-4s %016llX %016llX%s", kReg[r], (unsigned long long)q[1], (unsigned long long)q[0], (r & 1) ? "\n" : "   ");
    }
    o << fmt("pc %08X hi %016llX lo %016llX\n", c->pc, (unsigned long long)c->hi, (unsigned long long)c->lo);
    return o.str();
}

std::vector<std::string> logTail(size_t n)
{
    std::lock_guard<std::mutex> l(g_logMx);
    const size_t s = g_log.size() > n ? g_log.size() - n : 0;
    return std::vector<std::string>(g_log.begin() + static_cast<long>(s), g_log.end());
}
void logClear() { std::lock_guard<std::mutex> l(g_logMx); g_log.clear(); }

// ---------------------------------------------------------------- memory search
namespace
{
std::vector<uint32_t> g_found;
std::vector<uint32_t> g_foundVal;
int g_findSize = 4;
std::mutex g_findMx;
uint32_t rdN(uint32_t a, int sz)
{
    uint8_t *r = g_ram.load();
    if (!r || a > 0x2000000u - static_cast<uint32_t>(sz)) return 0;
    uint32_t v = 0;
    std::memcpy(&v, r + a, static_cast<size_t>(sz));
    return v;
}
} // namespace

std::string findFirst(int sz, uint32_t val, uint32_t lo, uint32_t hi)
{
    uint8_t *r = g_ram.load();
    if (!r) return "not attached";
    std::lock_guard<std::mutex> l(g_findMx);
    g_found.clear(); g_foundVal.clear(); g_findSize = sz;
    hi = std::min<uint32_t>(hi, 0x2000000u);
    for (uint32_t a = lo; a + static_cast<uint32_t>(sz) <= hi; a += static_cast<uint32_t>(sz))
        if (rdN(a, sz) == val) { g_found.push_back(a); g_foundVal.push_back(val); if (g_found.size() >= 2000000) break; }
    return fmt("%zu matches", g_found.size());
}

std::string findBytes(const std::vector<uint8_t> &pat, uint32_t lo, uint32_t hi)
{
    uint8_t *r = g_ram.load();
    if (!r || pat.empty()) return "nothing to find";
    std::lock_guard<std::mutex> l(g_findMx);
    g_found.clear(); g_foundVal.clear(); g_findSize = 4;
    hi = std::min<uint32_t>(hi, 0x2000000u);
    for (uint32_t a = lo; a + pat.size() <= hi; ++a)
        if (std::memcmp(r + a, pat.data(), pat.size()) == 0) { g_found.push_back(a); g_foundVal.push_back(rdN(a, 4)); if (g_found.size() >= 100000) break; }
    return fmt("%zu matches", g_found.size());
}

// mode: eq ne inc dec changed same (value used by eq/ne)
std::string findNext(const std::string &mode, uint32_t val)
{
    std::lock_guard<std::mutex> l(g_findMx);
    std::vector<uint32_t> na, nv;
    for (size_t i = 0; i < g_found.size(); ++i)
    {
        const uint32_t a = g_found[i], old = g_foundVal[i], cur = rdN(a, g_findSize);
        bool keep = false;
        if (mode == "eq") keep = cur == val;
        else if (mode == "ne") keep = cur != val;
        else if (mode == "inc") keep = cur > old;
        else if (mode == "dec") keep = cur < old;
        else if (mode == "changed") keep = cur != old;
        else if (mode == "same") keep = cur == old;
        if (keep) { na.push_back(a); nv.push_back(cur); }
    }
    g_found.swap(na); g_foundVal.swap(nv);
    return fmt("%zu matches", g_found.size());
}

std::vector<std::pair<uint32_t, uint32_t>> findResults(size_t maxN)
{
    std::lock_guard<std::mutex> l(g_findMx);
    std::vector<std::pair<uint32_t, uint32_t>> o;
    for (size_t i = 0; i < g_found.size() && i < maxN; ++i) o.emplace_back(g_found[i], rdN(g_found[i], g_findSize));
    return o;
}
size_t findCount() { std::lock_guard<std::mutex> l(g_findMx); return g_found.size(); }

// ---------------------------------------------------------------- threads with call stacks
std::string threadsText()
{
    PS2Runtime *rt = g_rt.load();
    if (!rt) return "not attached\n";
    const EeKernelSnapshot s = rt->eeScheduler().snapshot();
    std::ostringstream o;
    o << "running=" << s.runningThreadId << "\n";
    for (const EeThreadSnapshot &t : s.threads)
    {
        o << fmt("th%-2d st=%d wait=%d id=%d prio=%d pc=%08X ra=%08X sp=%08X entry=%08X  stack:", t.id, (int)t.status, (int)t.waitReason, t.waitId, t.currentPriority, t.pc, t.ra, t.sp, t.entry);
        for (uint32_t r : backtrace(t.sp, 12)) o << " " << hex(r);
        o << "\n";
    }
    return o.str();
}

// ---------------------------------------------------------------- attach + console
void attach(PS2Runtime *rt)
{
    if (!rt) return;
    g_rt.store(rt);
    g_ram.store(rt->memory().getRDRAM());
}

static uint32_t parseHex(const std::string &s) { return static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16)); }

bool consoleCommand(uint8_t *rdram, const std::string &c, std::istream &ls, std::ostream &out)
{
    if (rdram && !g_ram.load()) g_ram.store(rdram);
    if (c == "bp" || c == "bpp" || c == "bpc")
    {
        // bp ADDR [aN=VAL]   log every call (args, return, call stack) to analysis/live/dbg.txt
        // bpp ADDR [aN=VAL]  pause the calling game thread on entry until "cont"
        // bpc ADDR           count calls only (logs the first hits and every power of two)
        std::string a, cond; ls >> a >> cond;
        bool hc = false; uint32_t cr = 4, cv = 0;
        if (cond.size() > 3 && cond[0] == 'a' && cond[2] == '=') { hc = true; cr = 4u + static_cast<uint32_t>(cond[1] - '0'); cv = parseHex(cond.substr(3)); }
        out << bpAdd(parseHex(a), c == "bpp" ? BpPause : (c == "bpc" ? BpCount : BpLog), hc, cr, cv) << "\n";
        return true;
    }
    if (c == "bpd") { std::string a; ls >> a; out << bpDel(a.empty() || a == "all" ? 0u : parseHex(a)) << "\n"; return true; }
    if (c == "bpl") { out << bpList(); for (const auto &s : logTail(20)) out << s << "\n"; return true; }
    if (c == "cont") { out << (isPaused() ? "resumed\n" : "not paused\n"); resume(); return true; }
    if (c == "regs") { out << regsText(); return true; }
    if (c == "dis")
    {
        std::string a; uint32_t n = 32; ls >> a >> n;
        uint32_t addr = parseHex(a) & ~3u; n = std::min<uint32_t>(n, 512);
        for (uint32_t i = 0; i < n; ++i, addr += 4)
        {
            const uint32_t w = rd32(addr);
            PS2Runtime *rt = g_rt.load();
            const bool fn = rt && rt->lookupFunction(addr) != nullptr;
            out << fmt("%s%08X: %08X  %s\n", fn ? "* " : "  ", addr, w, disasm(addr, w).c_str());
        }
        return true;
    }
    if (c == "bt") { std::string a; ls >> a; for (uint32_t r : backtrace(parseHex(a), 24)) out << hex(r) << " "; out << "\n"; return true; }
    if (c == "th" || c == "thbt") { out << threadsText(); return true; }
    if (c == "wp" || c == "wpr")
    {
        std::string a; uint32_t len = 4; ls >> a >> len;
        svr06HwWatchRwMode(c == "wpr" ? 1 : 0);
        svr06PageWatchSet(rdram ? rdram : g_ram.load(), parseHex(a) & 0x1FFFFFFu, len);
        out << (len ? "watch armed, hits go to analysis/live/pagew.txt\n" : "watch disarmed\n");
        return true;
    }
    if (c == "find")
    {
        // find u8|u16|u32|f32 VALUE [LO HI]  |  find str TEXT  |  find hex AABBCC
        std::string t, v, lo, hi; ls >> t >> v >> lo >> hi;
        const uint32_t L = lo.empty() ? 0x100000u : parseHex(lo), H = hi.empty() ? 0x2000000u : parseHex(hi);
        if (t == "str") out << findBytes(std::vector<uint8_t>(v.begin(), v.end()), L, H) << "\n";
        else if (t == "hex") { std::vector<uint8_t> p; for (size_t i = 0; i + 1 < v.size(); i += 2) p.push_back(static_cast<uint8_t>(std::strtoul(v.substr(i, 2).c_str(), nullptr, 16))); out << findBytes(p, L, H) << "\n"; }
        else if (t == "f32") { float f = std::strtof(v.c_str(), nullptr); uint32_t u; std::memcpy(&u, &f, 4); out << findFirst(4, u, L, H) << "\n"; }
        else out << findFirst(t == "u8" ? 1 : (t == "u16" ? 2 : 4), parseHex(v), L, H) << "\n";
        for (const auto &p : findResults(40)) out << fmt("  %08X = %X\n", p.first, p.second);
        return true;
    }
    if (c == "findnext")
    {
        std::string m, v; ls >> m >> v;
        out << findNext(m, parseHex(v)) << "\n";
        for (const auto &p : findResults(40)) out << fmt("  %08X = %X\n", p.first, p.second);
        return true;
    }
    if (c == "dbghelp")
    {
        out << "bp/bpp/bpc ADDR [aN=HEX]  bpd ADDR|all  bpl  cont  regs  dis ADDR [N]  bt SP  th  wp/wpr ADDR [LEN]\n"
               "find u8|u16|u32|f32 VAL [LO HI] | find str TEXT | find hex BYTES   findnext eq|ne|inc|dec|changed|same [VAL]\n";
        return true;
    }
    return false;
}
} // namespace svrdbg
