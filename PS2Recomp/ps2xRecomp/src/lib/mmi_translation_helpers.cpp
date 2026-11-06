#include "ps2recomp/code_generator.h"
#include "ps2recomp/codegen_helpers.h"
#include "ps2recomp/instructions.h"
#include "ps2recomp/types.h"
#include <fmt/format.h>
#include <sstream>
#include <cmath>

namespace ps2recomp
{
    std::string CodeGenerator::translateMMI0Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI0_PADDW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PMAXW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMAXW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PMAXH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMAXH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PCGTB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCGTB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PADDSW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSUBSW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PADDSB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epi8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PSUBSB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epi8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXTLB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTLB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PPACB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PPACB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI0_PEXT5:
            return translatePEXT5(inst);
        case MMI0_PPAC5:
            return translatePPAC5(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI0 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translateMMI1Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI1_PABSW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSW(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADSBH:
            return translatePADSBH(inst);
        case MMI1_PABSH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PABSH(GPR_VEC(ctx, {})));", rd, rs);
        case MMI1_PCEQH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PMINH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMINH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PCEQB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCEQB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUW:
            return fmt::format(
                "SET_GPR_VEC(ctx, {}, ps2_paddu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUW:
            return fmt::format(
                "SET_GPR_VEC(ctx, {}, ps2_psubu32(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_add_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_sub_epi16(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PADDUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_adds_epu8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PSUBUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, _mm_subs_epu8(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_PEXTUB:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXTUB(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI1_QFSRV:
            return translateQFSRV(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI1 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translateMMI2Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI2_PMADDW:
            return translatePMADDW(inst);
        case MMI2_PSLLVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSLLVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PSRLVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSRLVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMSUBW:
            return translatePMSUBW(inst);
        case MMI2_PMFHI:
            return rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)ctx->hi1, (int64_t)ctx->hi));", rd);
        case MMI2_PMFLO:
            return rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)ctx->lo1, (int64_t)ctx->lo));", rd);
        case MMI2_PINTH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMULTW:
            return translatePMULTW(inst);
        case MMI2_PDIVW:
            return translatePDIVW(inst);
        case MMI2_PCPYLD:
            return translatePCPYLD(inst);
        case MMI2_PAND:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PAND(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PXOR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PXOR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI2_PMADDH:
            return translatePMADDH(inst);
        case MMI2_PHMADH:
            return translatePHMADH(inst);
        case MMI2_PMSUBH:
            return translatePMSUBH(inst);
        case MMI2_PHMSBH:
            return translatePHMSBH(inst);
        case MMI2_PEXEH:
            return translatePEXEH(inst);
        case MMI2_PREVH:
            return translatePREVH(inst);
        case MMI2_PMULTH:
            return translatePMULTH(inst);
        case MMI2_PDIVBW:
            return translatePDIVBW(inst);
        case MMI2_PEXEW:
            return translatePEXEW(inst);
        case MMI2_PROT3W:
            return translatePROT3W(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI2 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translateMMI3Instruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        uint8_t rd = inst.rd;
        switch (subfunc)
        {
        case MMI3_PMADDUW:
            return translatePMADDUW(inst);
        case MMI3_PSRAVW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PSRAVW(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PMTHI:
            return translatePMTHI(inst);
        case MMI3_PMTLO:
            return translatePMTLO(inst);
        case MMI3_PINTEH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PINTEH(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PMULTUW:
            return translatePMULTUW(inst);
        case MMI3_PDIVUW:
            return translatePDIVUW(inst);
        case MMI3_PCPYUD:
            return translatePCPYUD(inst);
        case MMI3_POR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_POR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PNOR:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PNOR(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));", rd, rs, rt);
        case MMI3_PEXCH:
            return translatePEXCH(inst);
        case MMI3_PCPYH:
            return translatePCPYH(inst);
        case MMI3_PEXCW:
            return translatePEXCW(inst);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled MMI3 instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translatePMFHLInstruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        switch (subfunc)
        {
        case PMFHL_LW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_LW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_UW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_UW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_SLW:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_SLW(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_LH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_LH(ctx->hi, ctx->lo));", inst.rd);
        case PMFHL_SH:
            return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PMFHL_SH(ctx->hi, ctx->lo));", inst.rd);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled PMFHL instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translatePMTHLInstruction(const Instruction &inst)
    {
        uint8_t subfunc = inst.sa;
        switch (subfunc)
        {
        case PMFHL_LW:
            return fmt::format("{{ __m128i val = GPR_VEC(ctx, {}); \n"
                               "   ctx->lo = (ctx->lo & 0xFFFFFFFF00000000ull) | (uint32_t)_mm_extract_epi32(val, 0); \n"
                               "   ctx->hi = (ctx->hi & 0xFFFFFFFF00000000ull) | (uint32_t)_mm_extract_epi32(val, 1); \n"
                               "   ctx->lo1 = (ctx->lo1 & 0xFFFFFFFF00000000ull) | (uint32_t)_mm_extract_epi32(val, 2); \n"
                               "   ctx->hi1 = (ctx->hi1 & 0xFFFFFFFF00000000ull) | (uint32_t)_mm_extract_epi32(val, 3); }}", inst.rs);
        default:
            return emitUnhandledInstruction(inst, fmt::format("Unhandled PMTHL instruction: function 0x{:X}", subfunc));
        }
    }


    std::string CodeGenerator::translatePEXT5(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i m1 = _mm_set1_epi32(0x0000001F); \n"
            "   __m128i m2 = _mm_set1_epi32(0x000003E0); \n"
            "   __m128i m3 = _mm_set1_epi32(0x00007C00); \n"
            "   __m128i m4 = _mm_set1_epi32(0x00008000); \n"
            "   __m128i a1 = _mm_slli_epi32(_mm_and_si128(rt, m1), 3); \n"
            "   __m128i a2 = _mm_slli_epi32(_mm_and_si128(rt, m2), 6); \n"
            "   __m128i a3 = _mm_slli_epi32(_mm_and_si128(rt, m3), 9); \n"
            "   __m128i a4 = _mm_slli_epi32(_mm_and_si128(rt, m4), 16); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_or_si128(_mm_or_si128(a1, a2), _mm_or_si128(a3, a4))); }}",
            inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePPAC5(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i m1 = _mm_set1_epi32(0x0000001F); \n"
            "   __m128i m2 = _mm_set1_epi32(0x000003E0); \n"
            "   __m128i m3 = _mm_set1_epi32(0x00007C00); \n"
            "   __m128i m4 = _mm_set1_epi32(0x00008000); \n"
            "   __m128i a1 = _mm_and_si128(_mm_srli_epi32(rt, 3), m1); \n"
            "   __m128i a2 = _mm_and_si128(_mm_srli_epi32(rt, 6), m2); \n"
            "   __m128i a3 = _mm_and_si128(_mm_srli_epi32(rt, 9), m3); \n"
            "   __m128i a4 = _mm_and_si128(_mm_srli_epi32(rt, 16), m4); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_or_si128(_mm_or_si128(a1, a2), _mm_or_si128(a3, a4))); }}",
            inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePADSBH(const Instruction &inst)
    {
        return fmt::format(
            "{{ __m128i rs = GPR_VEC(ctx, {}); __m128i rt = GPR_VEC(ctx, {}); \n"
            "   __m128i sub = _mm_sub_epi16(rs, rt); \n"
            "   __m128i add = _mm_add_epi16(rs, rt); \n"
            "   SET_GPR_VEC(ctx, {}, _mm_unpacklo_epi64(sub, _mm_unpackhi_epi64(add, add))); }}",
            inst.rs, inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePMADDW(const Instruction &inst)
    {
        // R5900 word multiply (pipeline 0 = words 0*0, pipeline 1 = words 2*2): result i goes to rd.d[i] and, split in two sign-extended
        // 32-bit halves, to LO.d[i] (lo/lo1) and HI.d[i] (hi/hi1); the accumulate forms start from (HI.w[2i]:LO.w[2i]).
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)r1, (int64_t)r0));", inst.rd);
        return fmt::format(
            "{{ alignas(16) uint32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {1})); \n"
            "   uint64_t p, a, r0, r1; \n"
            "   p = (uint64_t)((int64_t)(int32_t)s[0] * (int64_t)(int32_t)t[0]); a = ((ctx->hi & 0xFFFFFFFFull) << 32) | (ctx->lo & 0xFFFFFFFFull); r0 = a + p; \n"
            "   p = (uint64_t)((int64_t)(int32_t)s[2] * (int64_t)(int32_t)t[2]); a = ((ctx->hi1 & 0xFFFFFFFFull) << 32) | (ctx->lo1 & 0xFFFFFFFFull); r1 = a + p; \n"
            "   ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)r0; ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(r0 >> 32); \n"
            "   ctx->lo1 = (uint64_t)(int64_t)(int32_t)(uint32_t)r1; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(uint32_t)(r1 >> 32); \n"
            "   {3} }}",
            inst.rs, inst.rt, inst.rd, rdSet);
    }


    std::string CodeGenerator::translatePMSUBW(const Instruction &inst)
    {
        // R5900 word multiply (pipeline 0 = words 0*0, pipeline 1 = words 2*2): result i goes to rd.d[i] and, split in two sign-extended
        // 32-bit halves, to LO.d[i] (lo/lo1) and HI.d[i] (hi/hi1); the accumulate forms start from (HI.w[2i]:LO.w[2i]).
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)r1, (int64_t)r0));", inst.rd);
        return fmt::format(
            "{{ alignas(16) uint32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {1})); \n"
            "   uint64_t p, a, r0, r1; \n"
            "   p = (uint64_t)((int64_t)(int32_t)s[0] * (int64_t)(int32_t)t[0]); a = ((ctx->hi & 0xFFFFFFFFull) << 32) | (ctx->lo & 0xFFFFFFFFull); r0 = a - p; \n"
            "   p = (uint64_t)((int64_t)(int32_t)s[2] * (int64_t)(int32_t)t[2]); a = ((ctx->hi1 & 0xFFFFFFFFull) << 32) | (ctx->lo1 & 0xFFFFFFFFull); r1 = a - p; \n"
            "   ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)r0; ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(r0 >> 32); \n"
            "   ctx->lo1 = (uint64_t)(int64_t)(int32_t)(uint32_t)r1; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(uint32_t)(r1 >> 32); \n"
            "   {3} }}",
            inst.rs, inst.rt, inst.rd, rdSet);
    }


    std::string CodeGenerator::translatePMULTW(const Instruction &inst)
    {
        // R5900 word multiply (pipeline 0 = words 0*0, pipeline 1 = words 2*2): result i goes to rd.d[i] and, split in two sign-extended
        // 32-bit halves, to LO.d[i] (lo/lo1) and HI.d[i] (hi/hi1); the accumulate forms start from (HI.w[2i]:LO.w[2i]).
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)r1, (int64_t)r0));", inst.rd);
        return fmt::format(
            "{{ alignas(16) uint32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {1})); \n"
            "   uint64_t p, a, r0, r1; \n"
            "   p = (uint64_t)((int64_t)(int32_t)s[0] * (int64_t)(int32_t)t[0]); a = ((ctx->hi & 0xFFFFFFFFull) << 32) | (ctx->lo & 0xFFFFFFFFull); r0 = p; \n"
            "   p = (uint64_t)((int64_t)(int32_t)s[2] * (int64_t)(int32_t)t[2]); a = ((ctx->hi1 & 0xFFFFFFFFull) << 32) | (ctx->lo1 & 0xFFFFFFFFull); r1 = p; \n"
            "   ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)r0; ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(r0 >> 32); \n"
            "   ctx->lo1 = (uint64_t)(int64_t)(int32_t)(uint32_t)r1; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(uint32_t)(r1 >> 32); \n"
            "   {3} }}",
            inst.rs, inst.rt, inst.rd, rdSet);
    }


    std::string CodeGenerator::translatePMADDUW(const Instruction &inst)
    {
        // R5900 word multiply (pipeline 0 = words 0*0, pipeline 1 = words 2*2): result i goes to rd.d[i] and, split in two sign-extended
        // 32-bit halves, to LO.d[i] (lo/lo1) and HI.d[i] (hi/hi1); the accumulate forms start from (HI.w[2i]:LO.w[2i]).
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)r1, (int64_t)r0));", inst.rd);
        return fmt::format(
            "{{ alignas(16) uint32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {1})); \n"
            "   uint64_t p, a, r0, r1; \n"
            "   p = (uint64_t)(uint32_t)s[0] * (uint64_t)(uint32_t)t[0]; a = ((ctx->hi & 0xFFFFFFFFull) << 32) | (ctx->lo & 0xFFFFFFFFull); r0 = a + p; \n"
            "   p = (uint64_t)(uint32_t)s[2] * (uint64_t)(uint32_t)t[2]; a = ((ctx->hi1 & 0xFFFFFFFFull) << 32) | (ctx->lo1 & 0xFFFFFFFFull); r1 = a + p; \n"
            "   ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)r0; ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(r0 >> 32); \n"
            "   ctx->lo1 = (uint64_t)(int64_t)(int32_t)(uint32_t)r1; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(uint32_t)(r1 >> 32); \n"
            "   {3} }}",
            inst.rs, inst.rt, inst.rd, rdSet);
    }


    std::string CodeGenerator::translatePDIVW(const Instruction &inst)
    {
        // R5900 PDIVW: words 0 and 2 of rs / rt -> LO.d[i] = sext(quotient), HI.d[i] = sext(remainder); rd is NOT written.
        return fmt::format("{{ alignas(16) int32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {})); \n"
                           "   for (int i = 0; i < 2; ++i) {{ int32_t a = s[2 * i], b = t[2 * i], q, r; \n"
                           "      if (b == 0) {{ q = (a < 0) ? 1 : -1; r = a; }} else if (a == INT32_MIN && b == -1) {{ q = INT32_MIN; r = 0; }} else {{ q = a / b; r = a % b; }} \n"
                           "      if (i == 0) {{ ctx->lo = (uint64_t)(int64_t)q; ctx->hi = (uint64_t)(int64_t)r; }} else {{ ctx->lo1 = (uint64_t)(int64_t)q; ctx->hi1 = (uint64_t)(int64_t)r; }} }} }}",
                           inst.rs, inst.rt);
    }


    std::string CodeGenerator::translatePCPYLD(const Instruction &inst)
    {
        // PCPYLD uses rs as the upper source and rt as the lower source.
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PCPYLD(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt);
    }


    std::string CodeGenerator::translatePMADDH(const Instruction &inst)
    {
        // PMADDH: as PMULTH but accumulated into the current HI/LO lanes (32-bit wrap)
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format(
            "SET_GPR_VEC(ctx, {}, _mm_set_epi32((int)o[6], (int)o[4], (int)o[2], (int)o[0]));", inst.rd);
        return fmt::format(
"{{ alignas(16) int16_t ha[8], hb[8]; \n"
"   _mm_store_si128((__m128i*)ha, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)hb, GPR_VEC(ctx, {1})); \n"
"   int32_t p[8]; for (int i = 0; i < 8; ++i) p[i] = (int32_t)ha[i] * (int32_t)hb[i]; \n"
"   uint32_t o[8]; /* LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3 */ \n"
"   o[0] = (uint32_t)ctx->lo; o[1] = (uint32_t)(ctx->lo >> 32); o[2] = (uint32_t)ctx->hi; o[3] = (uint32_t)(ctx->hi >> 32); \n"
"   o[4] = (uint32_t)ctx->lo1; o[5] = (uint32_t)(ctx->lo1 >> 32); o[6] = (uint32_t)ctx->hi1; o[7] = (uint32_t)(ctx->hi1 >> 32); \n"
"   for (int i = 0; i < 8; ++i) o[i] += (uint32_t)p[i]; \n"
"   ctx->lo = (uint64_t)o[0] | ((uint64_t)o[1] << 32); ctx->hi = (uint64_t)o[2] | ((uint64_t)o[3] << 32); \n"
"   ctx->lo1 = (uint64_t)o[4] | ((uint64_t)o[5] << 32); ctx->hi1 = (uint64_t)o[6] | ((uint64_t)o[7] << 32); \n"
"   {2} }}",
            inst.rs, inst.rt, rdSet);
    }


    std::string CodeGenerator::translatePHMADH(const Instruction &inst)
    {
        // PHMADH: pair sums p0+p1,p2+p3,p4+p5,p6+p7 -> LO.w0 HI.w0 LO.w2 HI.w2 (odd lanes get p1,p3,p5,p7, unverified; no accumulation); rd = {LO.w0, HI.w0, LO.w2, HI.w2}
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format(
            "SET_GPR_VEC(ctx, {}, _mm_set_epi32((int)o[6], (int)o[4], (int)o[2], (int)o[0]));", inst.rd);
        return fmt::format(
"{{ alignas(16) int16_t ha[8], hb[8]; \n"
"   _mm_store_si128((__m128i*)ha, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)hb, GPR_VEC(ctx, {1})); \n"
"   int32_t p[8]; for (int i = 0; i < 8; ++i) p[i] = (int32_t)ha[i] * (int32_t)hb[i]; \n"
"   uint32_t o[8]; /* LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3 */ \n"
"   o[0]=(uint32_t)(p[0]+p[1]); o[1]=(uint32_t)p[1]; o[2]=(uint32_t)(p[2]+p[3]); o[3]=(uint32_t)p[3]; \n"
"   o[4]=(uint32_t)(p[4]+p[5]); o[5]=(uint32_t)p[5]; o[6]=(uint32_t)(p[6]+p[7]); o[7]=(uint32_t)p[7]; \n"
"   ctx->lo = (uint64_t)o[0] | ((uint64_t)o[1] << 32); ctx->hi = (uint64_t)o[2] | ((uint64_t)o[3] << 32); \n"
"   ctx->lo1 = (uint64_t)o[4] | ((uint64_t)o[5] << 32); ctx->hi1 = (uint64_t)o[6] | ((uint64_t)o[7] << 32); \n"
"   {2} }}",
            inst.rs, inst.rt, rdSet);
    }


    std::string CodeGenerator::translatePMSUBH(const Instruction &inst)
    {
        // PMSUBH: as PMADDH but subtracting
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format(
            "SET_GPR_VEC(ctx, {}, _mm_set_epi32((int)o[6], (int)o[4], (int)o[2], (int)o[0]));", inst.rd);
        return fmt::format(
"{{ alignas(16) int16_t ha[8], hb[8]; \n"
"   _mm_store_si128((__m128i*)ha, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)hb, GPR_VEC(ctx, {1})); \n"
"   int32_t p[8]; for (int i = 0; i < 8; ++i) p[i] = (int32_t)ha[i] * (int32_t)hb[i]; \n"
"   uint32_t o[8]; /* LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3 */ \n"
"   o[0] = (uint32_t)ctx->lo; o[1] = (uint32_t)(ctx->lo >> 32); o[2] = (uint32_t)ctx->hi; o[3] = (uint32_t)(ctx->hi >> 32); \n"
"   o[4] = (uint32_t)ctx->lo1; o[5] = (uint32_t)(ctx->lo1 >> 32); o[6] = (uint32_t)ctx->hi1; o[7] = (uint32_t)(ctx->hi1 >> 32); \n"
"   for (int i = 0; i < 8; ++i) o[i] -= (uint32_t)p[i]; \n"
"   ctx->lo = (uint64_t)o[0] | ((uint64_t)o[1] << 32); ctx->hi = (uint64_t)o[2] | ((uint64_t)o[3] << 32); \n"
"   ctx->lo1 = (uint64_t)o[4] | ((uint64_t)o[5] << 32); ctx->hi1 = (uint64_t)o[6] | ((uint64_t)o[7] << 32); \n"
"   {2} }}",
            inst.rs, inst.rt, rdSet);
    }


    std::string CodeGenerator::translatePHMSBH(const Instruction &inst)
    {
        // PHMSBH: pair differences p0-p1,p2-p3,p4-p5,p6-p7 -> LO.w0 HI.w0 LO.w2 HI.w2; odd lanes ~p (undocumented, unverified)
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format(
            "SET_GPR_VEC(ctx, {}, _mm_set_epi32((int)o[6], (int)o[4], (int)o[2], (int)o[0]));", inst.rd);
        return fmt::format(
"{{ alignas(16) int16_t ha[8], hb[8]; \n"
"   _mm_store_si128((__m128i*)ha, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)hb, GPR_VEC(ctx, {1})); \n"
"   int32_t p[8]; for (int i = 0; i < 8; ++i) p[i] = (int32_t)ha[i] * (int32_t)hb[i]; \n"
"   uint32_t o[8]; /* LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3 */ \n"
"   o[0]=(uint32_t)(p[0]-p[1]); o[1]=~(uint32_t)p[1]; o[2]=(uint32_t)(p[2]-p[3]); o[3]=~(uint32_t)p[3]; \n"
"   o[4]=(uint32_t)(p[4]-p[5]); o[5]=~(uint32_t)p[5]; o[6]=(uint32_t)(p[6]-p[7]); o[7]=~(uint32_t)p[7]; \n"
"   ctx->lo = (uint64_t)o[0] | ((uint64_t)o[1] << 32); ctx->hi = (uint64_t)o[2] | ((uint64_t)o[3] << 32); \n"
"   ctx->lo1 = (uint64_t)o[4] | ((uint64_t)o[5] << 32); ctx->hi1 = (uint64_t)o[6] | ((uint64_t)o[7] << 32); \n"
"   {2} }}",
            inst.rs, inst.rt, rdSet);
    }


    std::string CodeGenerator::translatePEXEH(const Instruction &inst)
    {
        // Swaps halfwords 1<->3 and 5<->7 within the 128-bit register
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shufflelo_epi16(_mm_shufflehi_epi16(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1)));",
                           inst.rd, inst.rt);
    }


    std::string CodeGenerator::translatePREVH(const Instruction &inst)
    {
        // Reverses the order of the 8 halfwords
        return fmt::format("{{ __m128i mask = _mm_setr_epi8(14,15, 12,13, 10,11, 8,9, 6,7, 4,5, 2,3, 0,1); "
                           "SET_GPR_VEC(ctx, {}, PS2_SHUFFLE_EPI8(GPR_VEC(ctx, {}), mask)); }}",
                           inst.rd, inst.rt);
    }


    std::string CodeGenerator::translatePMULTH(const Instruction &inst)
    {
        // PMULTH: eight halfword products -> LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3; rd = {LO.w0, HI.w0, LO.w2, HI.w2}
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format(
            "SET_GPR_VEC(ctx, {}, _mm_set_epi32((int)o[6], (int)o[4], (int)o[2], (int)o[0]));", inst.rd);
        return fmt::format(
"{{ alignas(16) int16_t ha[8], hb[8]; \n"
"   _mm_store_si128((__m128i*)ha, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)hb, GPR_VEC(ctx, {1})); \n"
"   int32_t p[8]; for (int i = 0; i < 8; ++i) p[i] = (int32_t)ha[i] * (int32_t)hb[i]; \n"
"   uint32_t o[8]; /* LO.w0 LO.w1 HI.w0 HI.w1 LO.w2 LO.w3 HI.w2 HI.w3 */ \n"
"   o[0]=(uint32_t)p[0]; o[1]=(uint32_t)p[1]; o[2]=(uint32_t)p[2]; o[3]=(uint32_t)p[3]; \n"
"   o[4]=(uint32_t)p[4]; o[5]=(uint32_t)p[5]; o[6]=(uint32_t)p[6]; o[7]=(uint32_t)p[7]; \n"
"   ctx->lo = (uint64_t)o[0] | ((uint64_t)o[1] << 32); ctx->hi = (uint64_t)o[2] | ((uint64_t)o[3] << 32); \n"
"   ctx->lo1 = (uint64_t)o[4] | ((uint64_t)o[5] << 32); ctx->hi1 = (uint64_t)o[6] | ((uint64_t)o[7] << 32); \n"
"   {2} }}",
            inst.rs, inst.rt, rdSet);
    }


    std::string CodeGenerator::translatePDIVBW(const Instruction &inst)
    {
        // R5900 PDIVBW: the four words of rs divided by the signed halfword rt.h[0]; quotients -> LO.w[0..3] (lo, lo1), remainders -> HI.w[0..3] (hi, hi1); rd is NOT written.
        return fmt::format("{{ alignas(16) int32_t s[4]; alignas(16) uint32_t t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {})); \n"
                           "   int32_t b = (int16_t)(t[0] & 0xFFFF); uint32_t q[4], r[4]; \n"
                           "   for (int i = 0; i < 4; ++i) {{ int32_t a = s[i]; int32_t qq, rr; \n"
                           "      if (b == 0) {{ qq = (a < 0) ? 1 : -1; rr = a; }} else if (a == INT32_MIN && b == -1) {{ qq = INT32_MIN; rr = 0; }} else {{ qq = a / b; rr = a % b; }} \n"
                           "      q[i] = (uint32_t)qq; r[i] = (uint32_t)rr; }} \n"
                           "   ctx->lo = (uint64_t)q[0] | ((uint64_t)q[1] << 32); ctx->lo1 = (uint64_t)q[2] | ((uint64_t)q[3] << 32); \n"
                           "   ctx->hi = (uint64_t)r[0] | ((uint64_t)r[1] << 32); ctx->hi1 = (uint64_t)r[2] | ((uint64_t)r[3] << 32); }}",
                           inst.rs, inst.rt);
    }


    std::string CodeGenerator::translatePEXEW(const Instruction &inst)
    {
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_PEXEW(GPR_VEC(ctx, {})));",
                           inst.rd, inst.rt);
    }


    std::string CodeGenerator::translatePROT3W(const Instruction &inst)
    {
        // Rotates words left by 3: [d,c,b,a] -> [a,d,c,b]
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(0,3,2,1)));",
                           inst.rd, inst.rt);
    }


    std::string CodeGenerator::translatePMULTUW(const Instruction &inst)
    {
        // R5900 word multiply (pipeline 0 = words 0*0, pipeline 1 = words 2*2): result i goes to rd.d[i] and, split in two sign-extended
        // 32-bit halves, to LO.d[i] (lo/lo1) and HI.d[i] (hi/hi1); the accumulate forms start from (HI.w[2i]:LO.w[2i]).
        const std::string rdSet = inst.rd == 0 ? std::string() : fmt::format("SET_GPR_VEC(ctx, {}, _mm_set_epi64x((int64_t)r1, (int64_t)r0));", inst.rd);
        return fmt::format(
            "{{ alignas(16) uint32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {0})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {1})); \n"
            "   uint64_t p, a, r0, r1; \n"
            "   p = (uint64_t)(uint32_t)s[0] * (uint64_t)(uint32_t)t[0]; a = ((ctx->hi & 0xFFFFFFFFull) << 32) | (ctx->lo & 0xFFFFFFFFull); r0 = p; \n"
            "   p = (uint64_t)(uint32_t)s[2] * (uint64_t)(uint32_t)t[2]; a = ((ctx->hi1 & 0xFFFFFFFFull) << 32) | (ctx->lo1 & 0xFFFFFFFFull); r1 = p; \n"
            "   ctx->lo = (uint64_t)(int64_t)(int32_t)(uint32_t)r0; ctx->hi = (uint64_t)(int64_t)(int32_t)(uint32_t)(r0 >> 32); \n"
            "   ctx->lo1 = (uint64_t)(int64_t)(int32_t)(uint32_t)r1; ctx->hi1 = (uint64_t)(int64_t)(int32_t)(uint32_t)(r1 >> 32); \n"
            "   {3} }}",
            inst.rs, inst.rt, inst.rd, rdSet);
    }


    std::string CodeGenerator::translatePDIVUW(const Instruction &inst)
    {
        // R5900 PDIVUW: unsigned words 0 and 2; LO/HI doublewords hold the sign-extended 32-bit quotient / remainder; division by zero -> LO = -1, HI = rs; rd is NOT written.
        return fmt::format("{{ alignas(16) uint32_t s[4], t[4]; _mm_store_si128((__m128i*)s, GPR_VEC(ctx, {})); _mm_store_si128((__m128i*)t, GPR_VEC(ctx, {})); \n"
                           "   for (int i = 0; i < 2; ++i) {{ uint32_t a = s[2 * i], b = t[2 * i], q, r; \n"
                           "      if (b == 0) {{ q = 0xFFFFFFFFu; r = a; }} else {{ q = a / b; r = a % b; }} \n"
                           "      if (i == 0) {{ ctx->lo = (uint64_t)(int64_t)(int32_t)q; ctx->hi = (uint64_t)(int64_t)(int32_t)r; }} else {{ ctx->lo1 = (uint64_t)(int64_t)(int32_t)q; ctx->hi1 = (uint64_t)(int64_t)(int32_t)r; }} }} }}",
                           inst.rs, inst.rt);
    }


    std::string CodeGenerator::translatePCPYUD(const Instruction &inst)
    {
        // Copies upper 64 of rs to lower 64 of rd, upper 64 of rt to upper 64 of rd
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_unpackhi_epi64(GPR_VEC(ctx, {}), GPR_VEC(ctx, {})));",
                           inst.rd, inst.rs, inst.rt); // Order matters
    }


    std::string CodeGenerator::translatePEXCH(const Instruction &inst)
    {
        // Parallel Exchange Center Halfword (same as MMI2 PEXEH)
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shufflelo_epi16(_mm_shufflehi_epi16(GPR_VEC(ctx, {}), _MM_SHUFFLE(2,3,0,1)), _MM_SHUFFLE(2,3,0,1)));",
                           inst.rd, inst.rt);
    }


    std::string CodeGenerator::translatePCPYH(const Instruction &inst)
    {
        // Parallel Copy Halfword (Broadcast lower 16 bits of each 64-bit half)
        return fmt::format("{{ __m128i src = GPR_VEC(ctx, {}); uint16_t l = _mm_extract_epi16(src, 0); uint16_t h = _mm_extract_epi16(src, 4); \n"
                           "   SET_GPR_VEC(ctx, {}, _mm_set_epi16(h,h,h,h, l,l,l,l)); }}",
                           inst.rt, inst.rd);
    }


    std::string CodeGenerator::translatePEXCW(const Instruction &inst)
    {
        // Parallel Exchange Center Word (Swaps words 0<>2, 1<>3)
        return fmt::format("SET_GPR_VEC(ctx, {}, _mm_shuffle_epi32(GPR_VEC(ctx, {}), _MM_SHUFFLE(1,0,3,2)));",
                           inst.rd, inst.rt);
    }


    std::string CodeGenerator::translatePMTHI(const Instruction &inst)
    {
        // R5900 PMTHI: the full 128-bit HI (hi = low doubleword, hi1 = high doubleword) is loaded from rs
        return fmt::format("{{ __m128i v = GPR_VEC(ctx, {}); ctx->hi = (uint64_t)_mm_cvtsi128_si64(v); ctx->hi1 = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(v, 8)); }}", inst.rs);
    }


    std::string CodeGenerator::translatePMTLO(const Instruction &inst)
    {
        return fmt::format("{{ __m128i v = GPR_VEC(ctx, {}); ctx->lo = (uint64_t)_mm_cvtsi128_si64(v); ctx->lo1 = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(v, 8)); }}", inst.rs);
    }


    std::string CodeGenerator::translateQFSRV(const Instruction &inst)
    {
        uint8_t rd = inst.rd;
        uint8_t rs = inst.rs;
        uint8_t rt = inst.rt;
        // QFSRV semantics are centralized in runtime macro helpers.
        return fmt::format("SET_GPR_VEC(ctx, {}, PS2_QFSRV(GPR_VEC(ctx, {}), GPR_VEC(ctx, {}), ctx->sa & 0x7F));",
                           rd, rs, rt);
    }

}
