// VU1 native-code generator (CL-0218). Offline tool, built in WSL against the runtime's own VU1 sources so the decode is the
// interpreter's decodeInstructionPair (no second decoder to keep in sync).
// Usage: gen out.cpp  image1.bin:pc,pc,...  [image2.bin:pc,...] ...
//   image = 16 KiB VU1 micro memory; a "VUD1" dump (analysis/live/vu1*.bin, 32-byte header + state + vi + data + code) is accepted too.
// Output: ps2_vu1_native_gen.cpp = one C++ function per (entry pc, code bytes) + the g_vu1NativeProgs table.
// Each instruction pair becomes the body of VU1Interpreter::runFast's loop with the pair's decode as constants; all state stays in the
// interpreter, so the function may return to the interpreter at any pair boundary. The output is derived from game code: local only.
#define private public
#define protected public
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_vu1.h"
#undef private
#undef protected
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
std::atomic<int> g_svr06Flags{0};
std::atomic<int> g_svr06DumpActive{0};
struct Svr06VifEv { uint32_t seq, kind, top, itop, startPC, nUnpack, qw, lo, hi; };
Svr06VifEv g_svr06VifRing[16]; uint32_t g_svr06VifRi = 0;
void svr06SamplerInit(const void *const *, const char *const *, int) {}
void GS::processGIFPacket(const uint8_t *, uint32_t) {}
void PS2Memory::submitGifPacket(GifPathId, const uint8_t *, uint32_t, bool, bool) {}

typedef VU1Interpreter::DecodedInstructionPair Dp;
static uint32_t kPairs = 2048u, kMask = 0x3FFFu; // CL-0371: 512 / 0xFFF in --vu0 mode (4 KiB VU0 micro memory)
static bool g_vu0 = false;

static std::vector<uint8_t> rd(const std::string &p)
{
    std::vector<uint8_t> v;
    if (FILE *f = fopen(p.c_str(), "rb")) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); v.resize(n); if (fread(v.data(), 1, n, f) != (size_t)n) v.clear(); fclose(f); }
    return v;
}
static bool branchCapable(const Dp &d) { return !d.iBit && (d.lKind >= 14u || d.lowerUsage.pipeline == VU1Interpreter::PipelineBranch); }
static bool staticBranch(const Dp &d) { return !d.iBit && d.lKind >= 14u && d.lKind <= 21u; }
static bool uncondBranch(const Dp &d) { return !d.iBit && (d.lKind == 20u || d.lKind == 21u); }
// D/T bit pairs are compiled as ordinary pairs: the interpreter only halts on them when the VU's D/T-enable flags (FBRST DE/TE) are set,
// so a program containing any D/T pair starts with a guard that returns to the interpreter in that case (see header emit in build()).
static bool compilable(const Dp &d) { return !d.anyReserved; }
static uint32_t targetOf(uint32_t pc, const Dp &d) { return (pc + 8u + (uint32_t)((int32_t)d.lImm * 8)) & kMask; }
static uint64_t fnv(const uint8_t *p, size_t n, uint64_t h = 1469598103934665603ull) { for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; } return h; }

struct Prog { uint32_t entry; std::vector<uint32_t> pairs; std::vector<uint32_t> words; uint64_t hash; std::string body; };

static std::string dset(const Dp &d, bool upper)
{
    char b[512];
    if (upper)
        std::snprintf(b, sizeof b, "Dp d; d.upper = 0x%08xu; d.uKind = %u; d.uOp = %u; d.uSrc = %u; d.uAcc = %s; d.uDest = %u; d.uFs = %u; d.uFt = %u; d.uFd = %u;",
                      d.upper, d.uKind, d.uOp, d.uSrc, d.uAcc ? "true" : "false", d.uDest, d.uFs, d.uFt, d.uFd);
    else
        std::snprintf(b, sizeof b, "Dp d; d.lower = 0x%08xu; d.lKind = %u; d.lA = %u; d.lB = %u; d.lC = %u; d.lDest = %u; d.lImm = %d;",
                      d.lower, d.lKind, d.lA, d.lB, d.lC, d.lDest, (int)d.lImm);
    return b;
}

static const char *kHelpers = R"VN(
    // FMAC result: normalise + MAC/status flags + store (= vu1fast::fmacFinish + VU1Interpreter::fmacStoreFast), with a fast path
    // for the common case that no dest lane has exponent 0 or 0xFF (then the result is stored as is and only the sign flags can be set).
    // nvr/valid/bit: shadow (normalised) copy of the destination register; bit == 0 for vf0.
    template <uint32_t DEST>
    static VU1_FORCE_INLINE void fin(VU1Interpreter &vu, float *dst, __m128 r, __m128 &nvr, uint64_t &valid, uint64_t bit)
    {
        const uint32_t dc = vu1fast::kRev4[DEST];
        const __m128i b = _mm_castps_si128(r);
        const __m128i expMask = _mm_set1_epi32(0x7F800000);
        const __m128i e = _mm_and_si128(b, expMask);
        const __m128i zl = _mm_cmpeq_epi32(_mm_andnot_si128(_mm_set1_epi32(static_cast<int>(0x80000000u)), b), _mm_setzero_si128()); // CL-0262: exact +-0 lanes (normalising them is a no-op)
        const __m128i sp = _mm_or_si128(_mm_andnot_si128(zl, _mm_cmpeq_epi32(e, _mm_setzero_si128())), _mm_cmpeq_epi32(e, expMask)); // CL-0262: only denormals and Inf/NaN take the slow path (zero results were ~6.5% of the VU1 worker)
        uint32_t mac, status;
        if (__builtin_expect((static_cast<uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(sp))) & dc) == 0u, 1))
        {
            if (DEST == 15u)
            {
                _mm_storeu_ps(dst, r);
                if (bit) { nvr = r; valid |= bit; }
            }
            else
            {
                const __m128i lm = _mm_load_si128(reinterpret_cast<const __m128i *>(vu1fast::kLaneMask[dc]));
                const __m128i old = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst));
                _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), _mm_or_si128(_mm_and_si128(lm, b), _mm_andnot_si128(lm, old)));
                if (valid & bit) nvr = _mm_castsi128_ps(_mm_or_si128(_mm_and_si128(lm, b), _mm_andnot_si128(lm, _mm_castps_si128(nvr))));
            }
            const uint32_t sM = static_cast<uint32_t>(_mm_movemask_ps(r)) & dc;
            const uint32_t zM = static_cast<uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(zl))) & dc; // CL-0262: Z flag for exact zeros
            mac = static_cast<uint32_t>(vu1fast::kRev4[zM]) | (static_cast<uint32_t>(vu1fast::kRev4[sM]) << 4);
            status = (zM ? 1u : 0u) | (sM ? 2u : 0u);
        }
        else
        {
            alignas(16) float res[4];
            _mm_store_ps(res, r);
            vu1fast::fmacFinish(dst, res, static_cast<uint8_t>(DEST), mac, status);
            valid &= ~bit;
        }
        if (!vu.m_skipFlags)
        {
            if (vu.m_lazyFlags)
            {
                VU1Interpreter::LazyFlag &lz = vu.m_lz[vu.m_lzCount & 3u];
                if (vu.m_lzCount >= 4u) { vu.m_lzRetMac = lz.mac; vu.m_lzRetCur = lz.cur; vu.m_lzSticky |= lz.cur; vu.m_lzHaveRet = true; }
                lz.issue = vu.m_cycle; lz.mac = mac; lz.cur = status & 0xFu;
                ++vu.m_lzCount;
            }
            else
                vu.pushFmacFlagsInl(mac, status);
        }
    }
    // VN_CHECK builds: every valid shadow must equal norm4(register)
    static void chk(VU1Interpreter &vu, const __m128 *nv, uint64_t valid, uint32_t pc)
    {
        for (uint32_t r = 0; r <= 32u; ++r)
        {
            if (!(valid & (1ull << r))) continue;
            const __m128 want = vu1inl::norm4(r == 32u ? vu.m_state.acc : vu.m_state.vf[r]);
            if (std::memcmp(&want, &nv[r], 16) != 0) { std::fprintf(stderr, "[vunat] SHADOW MISMATCH reg %u after pc %x\n", r, pc); std::abort(); }
        }
    }
)VN";

static bool build(const std::vector<uint8_t> &code, uint32_t entry, Prog &out)
{
    VU1Interpreter vu(g_vu0 ? VU1Interpreter::Unit::VU0 : VU1Interpreter::Unit::VU1);
    std::map<uint32_t, Dp> dec;            // compiled pairs by pc
    std::vector<uint32_t> work{entry & kMask & ~7u};
    std::set<uint32_t> expandNext;         // pcs whose fall-through successor is needed
    // state = (pc, followNext); a pair reached as the delay slot of an unconditional/dynamic branch or of an E-bit pair does not fall through
    std::set<std::pair<uint32_t, bool>> seen;
    std::vector<std::pair<uint32_t, bool>> st{{entry & kMask & ~7u, true}};
    while (!st.empty())
    {
        auto [pc, follow] = st.back(); st.pop_back();
        if (!seen.insert({pc, follow}).second) continue;
        if (!dec.count(pc))
        {
            Dp d = vu.decodeInstructionPair(code.data(), pc);
            if (!compilable(d)) continue;
            if (dec.size() >= 700u) return false;
            dec[pc] = d;
        }
        const Dp &d = dec[pc];
        const uint32_t next = (pc + 8u) & kMask;
        if (d.eBit) { st.push_back({next, false}); continue; }            // delay slot runs, then the program ends
        if (branchCapable(d))
        {
            if (staticBranch(d)) st.push_back({targetOf(pc, d), true});
            st.push_back({next, !uncondBranch(d) && staticBranch(d)});     // delay slot; falls through only after a conditional branch
            continue;
        }
        if (follow) st.push_back({next, true});
    }
    if (!dec.count(entry)) return false;
    // E-bit pair in a possible delay slot: every pair may then start with ebit set
    bool ebitAnywhere = false;
    for (auto &kv : dec) if (kv.second.eBit) { auto p = dec.find((kv.first - 8u) & kMask); if (p != dec.end() && branchCapable(p->second)) ebitAnywhere = true; }

    std::string o; char b[1024];
    auto go = [&](uint32_t pc) -> std::string { char t[64]; if (dec.count(pc)) std::snprintf(t, sizeof t, "goto P_%04x;", pc); else std::snprintf(t, sizeof t, "return 0;"); return t; };
    bool hasDT = false; for (auto &kv : dec) if (kv.second.dBit || kv.second.tBit) hasDT = true;
    std::snprintf(b, sizeof b, "    if (s.pc != 0x%04xu) return 0;\n%s    __m128 nv[33]; uint64_t nvV = 1ull; nv[0] = _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f);\n    goto P_%04x;\n", entry, hasDT ? "    if (s.dBitEnabled || s.tBitEnabled) return 0;\n" : "", entry); o += b;
    for (auto &kv : dec)
    {
        const uint32_t pc = kv.first; const Dp &d = kv.second;
        const uint32_t next = (pc + 8u) & kMask;
        auto prevIt = dec.find((pc - 8u) & kMask);
        const bool prevBranch = prevIt != dec.end() && branchCapable(prevIt->second);
        // the previous pair may also be an uncompiled branch: then this pair is only entered from the interpreter with branchPending
        // false (entry guard) or by a jump, so no pending branch can be live here.
        const bool mayEbit = ebitAnywhere || (prevIt != dec.end() && prevIt->second.eBit);
        std::snprintf(b, sizeof b, "P_%04x: // lower %08x upper %08x\n    VN_HEAD(0x%04xu)\n", pc, d.lower, d.upper, pc); o += b;
        if (d.fdivOrWaitQ) o += "    VN_FDIV()\n";
        o += "    VN_COMMIT()\n";
        if (d.delayViReg) { std::snprintf(b, sizeof b, "    { const int32_t oldVi = s.vi[%u];\n", d.delayViReg); o += b; } else o += "    {\n";
        uint64_t inval = 0;             // shadow registers (normalised operand cache) this pair makes stale
        bool genericAny = false;        // pair uses a generic handler (may write vf0 / vi0)
        bool writesVf0 = false;
        auto genUpperInval = [&]() { inval |= (1ull << ((d.upper >> 6) & 31u)) | (1ull << ((d.upper >> 11) & 31u)) | (1ull << ((d.upper >> 16) & 31u)) | (1ull << 32); genericAny = true; };
        auto genLowerInval = [&]() { inval |= (1ull << ((d.lower >> 6) & 31u)) | (1ull << ((d.lower >> 11) & 31u)) | (1ull << ((d.lower >> 16) & 31u)); genericAny = true; };
        if (d.iBit)
        {
            std::snprintf(b, sizeof b, "    vu.execUpper(0x%08xu); { const uint32_t ib = 0x%08xu; float im; std::memcpy(&im, &ib, 4); s.i = vu.normalizeOperand(im); }\n", d.upper, d.lower); o += b;
            genUpperInval();
        }
        else if (d.upperVfShadowReg != 0u)
        {
            std::snprintf(b, sizeof b, "    { float oldVf[4], upVf[4]; std::memcpy(oldVf, s.vf[%u], 16); vu.execUpper(0x%08xu); std::memcpy(upVf, s.vf[%u], 16); std::memcpy(s.vf[%u], oldVf, 16);\n"
                                       "      vu.execLower(0x%08xu, vuData, dataSize, gs, memory, 0x%08xu); std::memcpy(s.vf[%u], upVf, 16); }\n",
                          d.upperVfShadowReg, d.upper, d.upperVfShadowReg, d.upperVfShadowReg, d.lower, d.upper, d.upperVfShadowReg); o += b;
            genUpperInval(); genLowerInval();
        }
        else
        {
            if (d.uKind == 1u) { std::snprintf(b, sizeof b, "    vu.m_currentUpperInstruction = 0x%08xu;\n", d.upper); o += b; }
            else if (d.uKind == 2u)
            {   // FMAC arithmetic on the shadow (normalised) operands; same reads-then-write order as execUpperPre
                std::string bs;
                if (d.uSrc == 4u) { std::snprintf(b, sizeof b, "VN_N(%u)", d.uFt); bs = b; }
                else if (d.uSrc < 4u) { std::snprintf(b, sizeof b, "_mm_set1_ps(vu1inl::norm1(s.vf[%u][%u]))", d.uFt, d.uSrc); bs = b; }
                else bs = d.uSrc == 5u ? "_mm_set1_ps(vu1inl::norm1(s.q))" : "_mm_set1_ps(vu1inl::norm1(s.i))";
                const char *expr = d.uOp == 0u ? "_mm_add_ps(a, bb)" : d.uOp == 1u ? "_mm_sub_ps(a, bb)" : d.uOp == 2u ? "_mm_add_ps(VN_N(32), _mm_mul_ps(a, bb))" : d.uOp == 3u ? "_mm_sub_ps(VN_N(32), _mm_mul_ps(a, bb))" : "_mm_mul_ps(a, bb)";
                const uint32_t dreg = d.uAcc ? 32u : d.uFd;
                if (d.uDest == 0u) { std::snprintf(b, sizeof b, "    vu.m_currentUpperInstruction = 0x%08xu;\n", d.upper); o += b; }
                else
                {
                    const std::string dst = d.uAcc ? std::string("s.acc") : (std::string("s.vf[") + std::to_string(d.uFd) + "]");
                    const std::string bit = dreg == 0u ? std::string("0ull") : (std::string("(1ull << ") + std::to_string(dreg) + ")");
                    std::snprintf(b, sizeof b, "    { vu.m_currentUpperInstruction = 0x%08xu; const __m128 a = VN_N(%u); const __m128 bb = %s; const __m128 r = %s;\n      fin<%uu>(vu, %s, r, nv[%u], nvV, %s); }\n",
                                  d.upper, d.uFs, bs.c_str(), expr, d.uDest, dst.c_str(), dreg, bit.c_str()); o += b;
                    if (!d.uAcc && d.uFd == 0u) writesVf0 = true;
                }
            }
            else if (d.uKind >= 3u)
            {
                std::snprintf(b, sizeof b, "    { vu.m_currentUpperInstruction = 0x%08xu; %s vu.execUpperPreNoFlags(d); }\n", d.upper, dset(d, true).c_str()); o += b;
                const uint32_t dreg = (d.uKind == 5u || d.uKind == 6u) ? d.uFd : d.uFt;
                inval |= 1ull << dreg; if (dreg == 0u) writesVf0 = true;
            }
            else { std::snprintf(b, sizeof b, "    vu.execUpper(0x%08xu);\n", d.upper); o += b; genUpperInval(); }
            if (d.lKind >= 2u)
            {
                std::snprintf(b, sizeof b, "    { %s vu.execLowerPre(d, vuData, dataSize); }\n", dset(d, false).c_str()); o += b;
                if (d.lKind == 2u || d.lKind == 11u || d.lKind == 13u) { inval |= 1ull << d.lA; if (d.lA == 0u) writesVf0 = true; }
            }
            else if (d.lKind == 0u && (d.lower >> 25) == 0x04u)
            {   // CL-0230: ILW inline (same code as VU1Interpreter::execLower case 0x04; writes VI only)
                const uint32_t l = d.lower, it = (l >> 16) & 0xFu, is = (l >> 11) & 0xFu, dest = (l >> 21) & 0xFu;
                const int imm = (int16_t)(int32_t)((int32_t)(l << 21) >> 21);
                const int comp = (dest & 8u) ? 0 : (dest & 4u) ? 1 : (dest & 2u) ? 2 : 3;
                if (it != 0u) { std::snprintf(b, sizeof b, "    { uint32_t a = ((uint32_t)(int32_t)(s.vi[%u] + (%d))) * 16u; a &= (dataSize - 1); if (a + 16 <= dataSize) { uint32_t v; std::memcpy(&v, vuData + a + %d, 4); s.vi[%u] = (int32_t)(int16_t)(v & 0xFFFF); } }\n", is, imm, comp * 4, it); o += b; }
            }
            else if (d.lKind == 0u && (d.lower >> 25) == 0x05u)
            {   // CL-0230: ISW inline (execLower case 0x05)
                const uint32_t l = d.lower, it = (l >> 16) & 0xFu, is = (l >> 11) & 0xFu, dest = (l >> 21) & 0xFu;
                const int imm = (int16_t)(int32_t)((int32_t)(l << 21) >> 21);
                std::snprintf(b, sizeof b, "    { uint32_t a = ((uint32_t)(int32_t)(s.vi[%u] + (%d))) * 16u; a &= (dataSize - 1); if (a + 16 <= dataSize) { const uint32_t val = static_cast<uint32_t>(static_cast<uint16_t>(s.vi[%u] & 0xFFFF)); const uint32_t w[4] = {val, val, val, val}; vu.queueStore(a, w, %u); } }\n", is, imm, it, dest); o += b;
            }
            else if (d.lKind == 0u && (d.lower >> 25) == 0x12u)
            {   // CL-0230: FCAND inline (execLower case 0x12)
                std::snprintf(b, sizeof b, "    s.vi[1] = ((s.clip & 0x%06xu) != 0) ? 1 : 0;\n", d.lower & 0xFFFFFFu); o += b;
            }
            else if (d.lKind == 0u) { std::snprintf(b, sizeof b, "    vu.execLower(0x%08xu, vuData, dataSize, gs, memory, 0x%08xu);\n", d.lower, d.upper); o += b; genLowerInval(); }
        }
        inval &= ~1ull; // vf0 is constant at pair boundaries
        if (inval) { std::snprintf(b, sizeof b, "    nvV &= ~0x%llxull;\n", (unsigned long long)inval); o += b; }
        if (d.delayViReg) { std::snprintf(b, sizeof b, "    vu.m_viBranchBackupValid = false; vu.recordViWriteForBranch(%u, oldVi); }\n", d.delayViReg); o += b; }
        else o += "    vu.m_viBranchBackupValid = false; }\n";
        std::snprintf(b, sizeof b, (genericAny || writesVf0) ? "    VN_TAIL(0x%04xu)\n" : "    VN_TAILQ(0x%04xu)\n", next); o += b;
        std::snprintf(b, sizeof b, "    VN_CHK(0x%04xu)\n", pc); o += b;
        const bool branchy = branchCapable(d) || prevBranch;
        if (branchy) o += g_vu0 ? "    VN_BRANCH0()\n" : "    VN_BRANCH()\n";
        if (d.eBit) o += "    if (s.ebit) { ++vu.m_cycle; return 1; } s.ebit = true;\n";
        else if (mayEbit) o += "    if (s.ebit) { ++vu.m_cycle; return 1; }\n";
        o += "    ++vu.m_cycle;\n";
        if (!branchy) { o += "    " + go(next) + "\n"; }
        else if (prevBranch && !staticBranch(prevIt->second)) o += "    goto dispatch;\n";
        else if (prevBranch)
        {
            const uint32_t t = targetOf((pc - 8u) & kMask, prevIt->second);
            std::snprintf(b, sizeof b, "    if (s.pc == 0x%04xu) { %s } if (s.pc == 0x%04xu) { %s } return 0;\n", t, go(t).c_str(), next, go(next).c_str()); o += b;
        }
        else { o += "    " + go(next) + "\n"; }
    }
    o += "dispatch:\n    switch (s.pc)\n    {\n";
    for (auto &kv : dec) { std::snprintf(b, sizeof b, "    case 0x%04xu: goto P_%04x;\n", kv.first, kv.first); o += b; }
    o += "    default: return 0;\n    }\n";
    out.entry = entry; out.body = o;
    for (auto &kv : dec) { out.pairs.push_back(kv.first >> 3); out.words.push_back(kv.second.lower); out.words.push_back(kv.second.upper); }
    out.hash = fnv((const uint8_t *)out.pairs.data(), out.pairs.size() * 4u, fnv((const uint8_t *)out.words.data(), out.words.size() * 4u));
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: gen out.cpp image.bin:pc,pc ...\n"); return 2; }
    std::vector<Prog> progs, progs0; std::set<std::pair<uint32_t, uint64_t>> have, have0;
    for (int a = 2; a < argc; ++a)
    {
        std::string arg = argv[a];
        if (arg == "--vu0") { g_vu0 = true; kPairs = 512u; kMask = 0xFFFu; continue; }
        const size_t c = arg.rfind(':');
        if (c == std::string::npos) { fprintf(stderr, "bad arg %s\n", argv[a]); return 2; }
        std::vector<uint8_t> img = rd(arg.substr(0, c));
        if (img.size() >= 32u && std::memcmp(img.data(), "DVU1", 4) == 0)
        {   // VUD1 dump: hdr[8] (hdr[1] = sizeof state, hdr[6] = codeSize, hdr[7] = dataSize), state, vi[16], data, code
            uint32_t h[8]; std::memcpy(h, img.data(), 32);
            const size_t off = 32u + h[1] + 64u + h[7];
            if (off + 16384u > img.size()) { fprintf(stderr, "short dump %s\n", arg.c_str()); continue; }
            img = std::vector<uint8_t>(img.begin() + off, img.begin() + off + 16384);
        }
        if (g_vu0 && img.size() >= 4096u) img.resize(4096u); // vu0nat_*.bin = 4 KiB code + 4 KiB data
        if (img.size() != (g_vu0 ? 4096u : 16384u)) { fprintf(stderr, "skip %s (size %zu)\n", arg.c_str(), img.size()); continue; }
        std::string list = arg.substr(c + 1); size_t p = 0;
        while (p < list.size())
        {
            size_t e = list.find(',', p); if (e == std::string::npos) e = list.size();
            const uint32_t pc = (uint32_t)strtoul(list.substr(p, e - p).c_str(), nullptr, 16); p = e + 1;
            Prog pr;
            if (!build(img, pc, pr)) { fprintf(stderr, "  %s pc %04x: not compilable\n", arg.c_str(), pc); continue; }
            if (!(g_vu0 ? have0 : have).insert({pr.entry, pr.hash}).second) continue;
            fprintf(stderr, "  %s pc %04x: %zu pairs hash %016llx\n", g_vu0 ? "VU0" : "VU1", pc, pr.pairs.size(), (unsigned long long)pr.hash);
            (g_vu0 ? progs0 : progs).push_back(std::move(pr));
        }
    }
    FILE *f = fopen(argv[1], "w"); if (!f) return 1;
    fprintf(f, "// GENERATED by svr06-recomp/tools/vu1bench/gen.cpp (CL-0218) from the game's VU1 microcode. Do not edit; do not publish.\n");
    fprintf(f, "#include \"ps2_vu1_native.h\"\n#include <cstdio>\n#include <cstdlib>\n\nstruct Vu1Native\n{\n    typedef VU1Interpreter::DecodedInstructionPair Dp;\n");
    fputs(kHelpers, f);
    for (size_t i = 0; i < progs.size(); ++i) fprintf(f, "    static int f%zu(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint64_t limit);\n", i);
    for (size_t i = 0; i < progs0.size(); ++i) fprintf(f, "    static int g%zu(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint64_t limit);\n", i);
    fprintf(f, "};\n\n");
    for (size_t i = 0; i < progs.size(); ++i)
    {
        fprintf(f, "// entry 0x%04x, %zu pairs\nint Vu1Native::f%zu(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint64_t limit)\n{\n    VU1State &s = vu.m_state;\n%s}\n\n",
                progs[i].entry, progs[i].pairs.size(), i, progs[i].body.c_str());
        fprintf(f, "static const uint16_t kPairs%zu[] = {", i); for (uint32_t p : progs[i].pairs) fprintf(f, "%u,", p); fprintf(f, "};\n");
        fprintf(f, "static const uint32_t kWords%zu[] = {", i); for (uint32_t w : progs[i].words) fprintf(f, "0x%xu,", w); fprintf(f, "};\n\n");
    }
    fprintf(f, "extern const Vu1NativeProg g_vu1NativeProgs[] = {\n");
    for (size_t i = 0; i < progs.size(); ++i) fprintf(f, "    {0x%04xu, %zuu, kPairs%zu, kWords%zu, &Vu1Native::f%zu},\n", progs[i].entry, progs[i].pairs.size(), i, i, i);
    fprintf(f, "    {0u, 0u, nullptr, nullptr, nullptr}\n};\nextern const uint32_t g_vu1NativeProgCount = %zuu;\n\n", progs.size());
    // CL-0371: VU0 micro programs (4 KiB code memory, pc mask 0xFFF)
    for (size_t i = 0; i < progs0.size(); ++i)
    {
        fprintf(f, "// VU0 entry 0x%04x, %zu pairs\nint Vu1Native::g%zu(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint64_t limit)\n{\n    VU1State &s = vu.m_state;\n%s}\n\n",
                progs0[i].entry, progs0[i].pairs.size(), i, progs0[i].body.c_str());
        fprintf(f, "static const uint16_t kPairs0_%zu[] = {", i); for (uint32_t p : progs0[i].pairs) fprintf(f, "%u,", p); fprintf(f, "};\n");
        fprintf(f, "static const uint32_t kWords0_%zu[] = {", i); for (uint32_t w : progs0[i].words) fprintf(f, "0x%xu,", w); fprintf(f, "};\n\n");
    }
    fprintf(f, "extern const Vu1NativeProg g_vu0NativeProgs[] = {\n");
    for (size_t i = 0; i < progs0.size(); ++i) fprintf(f, "    {0x%04xu, %zuu, kPairs0_%zu, kWords0_%zu, &Vu1Native::g%zu},\n", progs0[i].entry, progs0[i].pairs.size(), i, i, i);
    fprintf(f, "    {0u, 0u, nullptr, nullptr, nullptr}\n};\nextern const uint32_t g_vu0NativeProgCount = %zuu;\n", progs0.size());
    fclose(f);
    fprintf(stderr, "wrote %s: %zu VU1 + %zu VU0 programs\n", argv[1], progs.size(), progs0.size());
    return 0;
}
