# one-off: rewrites gen.cpp (stage 1) into stage 2 (shadow operands, fast FMAC finish, lighter tails). Kept for the record (CL-0218).
p = "gen.cpp"
s = open(p, encoding="utf-8", newline="").read()


def rep(a, b, cnt=1):
    global s
    assert s.count(a) == cnt, (a, s.count(a))
    s = s.replace(a, b)


old_start = s.index("        if (d.iBit)\n        {\n            std::snprintf(b, sizeof b, \"    vu.execUpper(0x%08xu); { const uint32_t ib")
old_end = s.index("        if (d.delayViReg) { std::snprintf(b, sizeof b, \"    vu.m_viBranchBackupValid = false; vu.recordViWriteForBranch")
new = r'''        uint64_t inval = 0;             // shadow registers (normalised operand cache) this pair makes stale
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
            else if (d.lKind == 0u) { std::snprintf(b, sizeof b, "    vu.execLower(0x%08xu, vuData, dataSize, gs, memory, 0x%08xu);\n", d.lower, d.upper); o += b; genLowerInval(); }
        }
        inval &= ~1ull; // vf0 is constant at pair boundaries
        if (inval) { std::snprintf(b, sizeof b, "    nvV &= ~0x%llxull;\n", (unsigned long long)inval); o += b; }
'''
s = s[:old_start] + new + s[old_end:]

rep('''        std::snprintf(b, sizeof b, "    VN_TAIL(0x%04xu)\\n", next); o += b;''',
    '''        std::snprintf(b, sizeof b, (genericAny || writesVf0) ? "    VN_TAIL(0x%04xu)\\n" : "    VN_TAILQ(0x%04xu)\\n", next); o += b;
        std::snprintf(b, sizeof b, "    VN_CHK(0x%04xu)\\n", pc); o += b;''')

# entry: only the entry pc (the interpreter calls a program at its entry only)
a = s.index('    o += "    switch (s.pc)\\n    {\\n";')
e = s.index("    for (auto &kv : dec)\n    {\n        const uint32_t pc = kv.first; const Dp &d = kv.second;", a)
s = s[:a] + '''    std::snprintf(b, sizeof b, "    if (s.pc != 0x%04xu) return 0;\\n    __m128 nv[33]; uint64_t nvV = 1ull; nv[0] = _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f);\\n    goto P_%04x;\\n", entry, entry); o += b;\n''' + s[e:]

rep('''    for (size_t i = 0; i < progs.size(); ++i) fprintf(f, "    static int f%zu(''',
    '''    fputs(kHelpers, f);
    for (size_t i = 0; i < progs.size(); ++i) fprintf(f, "    static int f%zu(''')

helpers = r'''static const char *kHelpers = R"VN(
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
        const __m128i sp = _mm_or_si128(_mm_cmpeq_epi32(e, _mm_setzero_si128()), _mm_cmpeq_epi32(e, expMask));
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
            mac = static_cast<uint32_t>(vu1fast::kRev4[sM]) << 4;
            status = sM ? 2u : 0u;
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

'''
rep("static bool build(const std::vector<uint8_t> &code, uint32_t entry, Prog &out)", helpers + "static bool build(const std::vector<uint8_t> &code, uint32_t entry, Prog &out)")
rep('''    fprintf(f, "#include \\"ps2_vu1_native.h\\"\\n\\nstruct Vu1Native''',
    '''    fprintf(f, "#include \\"ps2_vu1_native.h\\"\\n#include <cstdio>\\n#include <cstdlib>\\n\\nstruct Vu1Native''')
# static-branch fallbacks leave to the interpreter instead of the dispatch switch
rep('''if (s.pc == 0x%04xu) { %s } if (s.pc == 0x%04xu) { %s } goto dispatch;\\n"''',
    '''if (s.pc == 0x%04xu) { %s } if (s.pc == 0x%04xu) { %s } return 0;\\n"''')
open(p, "w", encoding="utf-8", newline="").write(s)
print("stage 2 patch applied")
