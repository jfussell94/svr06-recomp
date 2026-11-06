#include "Common.h"
#include "VU.h"
#include "vu0_exact_gen.h"

namespace ps2_stubs
{
    namespace
    {
        // svr06 analysis aid (CL-0074): detect the moment the camera object's view matrix (0x62BDD0+0xE0) becomes 2^32-scaled garbage
        void svr06CamWatch(uint8_t *rdram, R5900Context *ctx, const char *name)
        {
            static const char *prevName = "";
            static uint32_t prevRa = 0, prevA0 = 0, prevA1 = 0, prevA2 = 0;
            static bool wasScaled = false;
            static int logged = 0;
            const uint32_t ra = getRegU32(ctx, 31);
            float v[4]{};
            std::memcpy(v, rdram + 0x62BEB0u, 16);
            const bool scaled = (std::fabs(v[0]) > 1.0e6f) || (std::fabs(v[1]) > 1.0e6f) || (std::fabs(v[2]) > 1.0e6f);
            if (scaled != wasScaled && logged < 40)
            {
                ++logged;
                std::fprintf(stderr, "[camwatch] cam+E0 %s: row0=%g %g %g (%08X %08X %08X) changed between %s(ra=%08X a0=%08X a1=%08X a2=%08X) and %s(ra=%08X a0=%08X a1=%08X a2=%08X)\n", scaled ? "SCALED" : "normal",
                             v[0], v[1], v[2], *(uint32_t *)&v[0], *(uint32_t *)&v[1], *(uint32_t *)&v[2], prevName, prevRa, prevA0, prevA1, prevA2, name, ra, getRegU32(ctx, 4), getRegU32(ctx, 5), getRegU32(ctx, 6));
            }
            wasScaled = scaled;
            prevName = name; prevRa = ra; prevA0 = getRegU32(ctx, 4); prevA1 = getRegU32(ctx, 5); prevA2 = getRegU32(ctx, 6);
        }
    }


    namespace
    {
        bool readVuVec4f(uint8_t *rdram, uint32_t addr, float (&out)[4])
        {
            const uint8_t *ptr = getConstMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(out, ptr, sizeof(out));
            return true;
        }

        bool writeVuVec4f(uint8_t *rdram, uint32_t addr, const float (&in)[4])
        {
            uint8_t *ptr = getMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(ptr, in, sizeof(in));
            return true;
        }

        bool readVuVec4i(uint8_t *rdram, uint32_t addr, int32_t (&out)[4])
        {
            const uint8_t *ptr = getConstMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(out, ptr, sizeof(out));
            return true;
        }

        bool writeVuVec4i(uint8_t *rdram, uint32_t addr, const int32_t (&in)[4])
        {
            uint8_t *ptr = getMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(ptr, in, sizeof(in));
            return true;
        }

        bool readVuMatrix4f(uint8_t *rdram, uint32_t addr, float (&out)[16])
        {
            const uint8_t *ptr = getConstMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(out, ptr, sizeof(out));
            return true;
        }

        bool writeVuMatrix4f(uint8_t *rdram, uint32_t addr, const float (&in)[16])
        {
            uint8_t *ptr = getMemPtr(rdram, addr);
            if (!ptr)
            {
                return false;
            }
            std::memcpy(ptr, in, sizeof(in));
            return true;
        }

        // Row-major matrix product: out = lhs * rhs (lhs is the left factor).
        void mulVuMatrix(const float (&lhs)[16], const float (&rhs)[16], float (&out)[16])
        {
            std::fill(std::begin(out), std::end(out), 0.0f);
            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 4; ++j)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        out[4 * i + j] += rhs[4 * k + j] * lhs[4 * i + k];
                    }
                }
            }
        }

        void makeIdentityMatrix(float (&out)[16])
        {
            std::fill(std::begin(out), std::end(out), 0.0f);
            out[0] = 1.0f;
            out[5] = 1.0f;
            out[10] = 1.0f;
            out[15] = 1.0f;
        }

        // Rigid-transform inverse under the file's row-vector convention
        // (translation in row 3, ApplyMatrix computes v*M): transpose the 3x3
        // rotation block, zero its 4th column, and set the translation row to
        // -t*R^T -- each lane dots t with a ROW of R: out[12+col] =
        // -(t . R[col]). This makes M * rigidInverse(M) == I. Copy [15].
        void rigidInverse(const float (&in)[16], float (&out)[16])
        {
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                    out[4 * row + col] = in[4 * col + row];
                out[4 * row + 3] = 0.0f;
            }
            const float tx = in[12], ty = in[13], tz = in[14];
            for (int col = 0; col < 3; ++col)
                out[12 + col] = -((tx * in[4 * col]) + (ty * in[4 * col + 1]) + (tz * in[4 * col + 2]));
            out[15] = in[15];
        }

        void axisRotateMatrix(const float (&in)[16], float angle, int axis, float (&out)[16])
        {
            float rot[16]{};
            makeIdentityMatrix(rot);
            const float cs = std::cos(angle);
            const float sn = std::sin(angle);
            if (axis == 2)
            {
                rot[0] = cs;
                rot[1] = sn;
                rot[4] = -sn;
                rot[5] = cs;
            }
            else if (axis == 1)
            {
                rot[0] = cs;
                rot[2] = -sn;
                rot[8] = sn;
                rot[10] = cs;
            }
            else
            {
                rot[5] = cs;
                rot[6] = sn;
                rot[9] = -sn;
                rot[10] = cs;
            }
            mulVuMatrix(in, rot, out);
        }

        // Applies the matrix to the vertex, perspective-divides xyz by w
        // (w==0 maps to a zero divide rather than Inf/NaN), then converts
        // x/y to 12.4 fixed point (x16) unconditionally. z/w take the same
        // x16 conversion when fullFtoi4 is set; otherwise they are plain
        // integer-truncated (FTOI0) after the divide instead.
        void rotTransPersOne(const float (&m)[16], const float (&v)[4], bool fullFtoi4, int32_t (&out)[4])
        {
            float t[4];
            t[0] = (m[0] * v[0]) + (m[4] * v[1]) + (m[8] * v[2]) + (m[12] * v[3]);
            t[1] = (m[1] * v[0]) + (m[5] * v[1]) + (m[9] * v[2]) + (m[13] * v[3]);
            t[2] = (m[2] * v[0]) + (m[6] * v[1]) + (m[10] * v[2]) + (m[14] * v[3]);
            t[3] = (m[3] * v[0]) + (m[7] * v[1]) + (m[11] * v[2]) + (m[15] * v[3]);
            const float q = (t[3] != 0.0f) ? (1.0f / t[3]) : 0.0f;
            t[0] *= q;
            t[1] *= q;
            t[2] *= q;
            out[0] = static_cast<int32_t>(t[0] * 16.0f);
            out[1] = static_cast<int32_t>(t[1] * 16.0f);
            out[2] = fullFtoi4 ? static_cast<int32_t>(t[2] * 16.0f) : static_cast<int32_t>(t[2]);
            out[3] = fullFtoi4 ? static_cast<int32_t>(t[3] * 16.0f) : static_cast<int32_t>(t[3]);
        }

        // Guard-band proxy for the COP2 sticky clip flags: nonzero => the
        // vertex is offscreen. Not the hardware per-plane flag layout.
        constexpr float kScreenClipGuard = 4096.0f;
        int32_t screenClipCode(const float (&v)[4])
        {
            int32_t code = 0;
            if (v[0] > kScreenClipGuard)
                code |= 0x1;
            if (v[0] < -kScreenClipGuard)
                code |= 0x2;
            if (v[1] > kScreenClipGuard)
                code |= 0x4;
            if (v[1] < -kScreenClipGuard)
                code |= 0x8;
            return code;
        }
    }

    void sceVpu0Reset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        setReturnS32(ctx, 0);
    }

    void sceVu0AddVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "AddVector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t lhsAddr = getRegU32(ctx, 5);
        const uint32_t rhsAddr = getRegU32(ctx, 6);
        float lhs[4]{}, rhs[4]{}, out[4]{};
        if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = lhs[i] + rhs[i];
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ApplyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ApplyMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E0E8(rdram, ctx, runtime);
    }

    void sceVu0CameraMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "CameraMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E720(rdram, ctx, runtime);
    }

    void sceVu0ClampVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ClampVector");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E6F8(rdram, ctx, runtime);
    }

    void sceVu0ClipAll(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ClipAll");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011ED40(rdram, ctx, runtime);
    }

    void sceVu0ClipScreen(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ClipScreen");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011EC98(rdram, ctx, runtime);
    }

    void sceVu0ClipScreen3(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ClipScreen3");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011ECE0(rdram, ctx, runtime);
    }

    void sceVu0CopyMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "CopyMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E3A8(rdram, ctx, runtime);
    }

    void sceVu0CopyVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "CopyVector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        uint8_t *dst = getMemPtr(rdram, dstAddr);
        const uint8_t *src = getConstMemPtr(rdram, srcAddr);
        if (dst && src)
        {
            std::memcpy(dst, src, sizeof(float) * 4u);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0CopyVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "CopyVectorXYZ");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        uint8_t *dst = getMemPtr(rdram, dstAddr);
        const uint8_t *src = getConstMemPtr(rdram, srcAddr);
        if (dst && src)
        {
            std::memcpy(dst, src, sizeof(float) * 3u);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0DivVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "DivVector");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E2A0(rdram, ctx, runtime);
    }

    void sceVu0DivVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "DivVectorXYZ");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E2C0(rdram, ctx, runtime);
    }

    void sceVu0DropShadowMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "DropShadowMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011EA00(rdram, ctx, runtime);
    }

    void sceVu0ecossin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ecossin");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E438(rdram, ctx, runtime);
    }

    void sceVu0FTOI0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "FTOI0Vector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        float src[4]{};
        int32_t out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = static_cast<int32_t>(src[i]);
            }
            (void)writeVuVec4i(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0FTOI4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "FTOI4Vector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        float src[4]{};
        int32_t out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = static_cast<int32_t>(src[i] * 16.0f);
            }
            (void)writeVuVec4i(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0InnerProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "InnerProduct");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E180(rdram, ctx, runtime);
    }

    void sceVu0InterVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "InterVector");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E2E0(rdram, ctx, runtime);
    }

    void sceVu0InterVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "InterVectorXYZ");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011EC50(rdram, ctx, runtime);
    }

    void sceVu0InversMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "InversMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E230(rdram, ctx, runtime);
    }

    void sceVu0ITOF0Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ITOF0Vector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        int32_t src[4]{};
        float out[4]{};
        if (readVuVec4i(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = static_cast<float>(src[i]);
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ITOF12Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ITOF12Vector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        int32_t src[4]{};
        float out[4]{};
        if (readVuVec4i(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = static_cast<float>(src[i]) / 4096.0f;
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ITOF4Vector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ITOF4Vector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        int32_t src[4]{};
        float out[4]{};
        if (readVuVec4i(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = static_cast<float>(src[i]) / 16.0f;
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0LightColorMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "LightColorMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E890(rdram, ctx, runtime);
    }

    void sceVu0MulMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "MulMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E118(rdram, ctx, runtime);
    }

    void sceVu0MulVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "MulVector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t lhsAddr = getRegU32(ctx, 5);
        const uint32_t rhsAddr = getRegU32(ctx, 6);
        float lhs[4]{}, rhs[4]{}, out[4]{};
        if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = lhs[i] * rhs[i];
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0Normalize(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "Normalize");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        const uint32_t nrmRa_ = getRegU32(ctx, 31), nrmDst_ = getRegU32(ctx, 4), nrmSrc_ = getRegU32(ctx, 5);
        float nrmIn_[4]{}; (void)readVuVec4f(rdram, nrmSrc_, nrmIn_);
        vu0x::fn_0011E1A8(rdram, ctx, runtime);
        {   // svr06 analysis aid (CL-0074): log Normalize calls whose output is not unit length
            float o_[4]{}; (void)readVuVec4f(rdram, nrmDst_, o_);
            const double l2_ = (double)o_[0] * o_[0] + (double)o_[1] * o_[1] + (double)o_[2] * o_[2];
            static int nrmLogged_ = 0;
            if ((!(l2_ > 0.9 && l2_ < 1.1)) && nrmLogged_ < 60)
            {
                ++nrmLogged_;
                std::fprintf(stderr, "[normwatch] ra=%08X src=%08X in=%g %g %g %g (%08X %08X %08X) -> out=%g %g %g\n", nrmRa_, nrmSrc_,
                             nrmIn_[0], nrmIn_[1], nrmIn_[2], nrmIn_[3], *(uint32_t *)&nrmIn_[0], *(uint32_t *)&nrmIn_[1], *(uint32_t *)&nrmIn_[2], o_[0], o_[1], o_[2]);
            }
        }
    }

    void sceVu0NormalLightMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "NormalLightMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E7D0(rdram, ctx, runtime);
    }

    void sceVu0OuterProduct(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "OuterProduct");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t lhsAddr = getRegU32(ctx, 5);
        const uint32_t rhsAddr = getRegU32(ctx, 6);
        float lhs[4]{}, rhs[4]{}, out[4]{};
        if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
        {
            out[0] = (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]);
            out[1] = (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]);
            out[2] = (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]);
            out[3] = 0.0f;
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0RotMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "RotMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E6A8(rdram, ctx, runtime);
    }

    void sceVu0RotMatrixX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "RotMatrixX");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E558(rdram, ctx, runtime);
    }

    void sceVu0RotMatrixY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "RotMatrixY");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E600(rdram, ctx, runtime);
    }

    void sceVu0RotMatrixZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "RotMatrixZ");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E4B0(rdram, ctx, runtime);
    }

    void sceVu0RotTransPers(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "RotTransPers");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011EBE8(rdram, ctx, runtime);
    }

    void sceVu0RotTransPersN(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "RotTransPersN");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011EB90(rdram, ctx, runtime);
    }

    void sceVu0ScaleVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ScaleVector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        float src[4]{}, out[4]{};
        float scale = ctx ? ctx->f[12] : 0.0f;
        if (scale == 0.0f)
        {
            uint32_t raw = getRegU32(ctx, 6);
            std::memcpy(&scale, &raw, sizeof(scale));
            if (scale == 0.0f)
            {
                scale = static_cast<float>(getRegU32(ctx, 6));
            }
        }

        if (readVuVec4f(rdram, srcAddr, src))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = src[i] * scale;
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0ScaleVectorXYZ(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ScaleVectorXYZ");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t srcAddr = getRegU32(ctx, 5);
        const float scale = ctx ? ctx->f[12] : 0.0f;
        float src[4]{}, out[4]{};
        if (readVuVec4f(rdram, srcAddr, src))
        {
            out[0] = src[0] * scale;
            out[1] = src[1] * scale;
            out[2] = src[2] * scale;
            out[3] = src[3];
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0SubVector(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "SubVector");
        const uint32_t dstAddr = getRegU32(ctx, 4);
        const uint32_t lhsAddr = getRegU32(ctx, 5);
        const uint32_t rhsAddr = getRegU32(ctx, 6);
        float lhs[4]{}, rhs[4]{}, out[4]{};
        if (readVuVec4f(rdram, lhsAddr, lhs) && readVuVec4f(rdram, rhsAddr, rhs))
        {
            for (int i = 0; i < 4; ++i)
            {
                out[i] = lhs[i] - rhs[i];
            }
            (void)writeVuVec4f(rdram, dstAddr, out);
        }
        setReturnS32(ctx, 0);
    }

    void sceVu0TransMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "TransMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E368(rdram, ctx, runtime);
    }

    void sceVu0TransposeMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "TransposeMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E1E8(rdram, ctx, runtime);
    }

    void sceVu0UnitMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "UnitMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E410(rdram, ctx, runtime);
    }

    void sceVu0ViewScreenMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        svr06CamWatch(rdram, ctx, "ViewScreenMatrix");
        // Exact transliteration of the game's own routine (tools/codeaudit/vu0_transpile.py),
        // verified bit-exact against the original code by tools/codeaudit/verify_vu0gen.py.
        vu0x::fn_0011E8F8(rdram, ctx, runtime);
    }
}
