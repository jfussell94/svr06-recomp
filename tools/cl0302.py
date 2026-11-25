#!/usr/bin/env python3
# CL-0302 audio + titantron audit fixes. usage: cl0302.py <tree root (PS2Recomp_usa or PS2Recomp)>
import sys, os, shutil
root = sys.argv[1]
P = {
 'ovr':   'ps2xRuntime/src/runner/svr06_overrides.cpp',
 'music': 'ps2xRuntime/src/lib/Kernel/Stubs/SvrMusic.cpp',
 'movie': 'ps2xRuntime/src/lib/Kernel/Stubs/SvrMovie.cpp',
 'rpc':   'ps2xRuntime/src/lib/Kernel/Syscalls/RPC.cpp',
 'spu':   'ps2xIOP/src/emulator/core/iop_spu2.cpp',
}
E = {}
E['ovr'] = [
 ("        static uint32_t s_playObj = 0;\n",
  "        static uint32_t s_playObj = 0;\n        static bool s_titanInWorld = false; // CL-0302: in-world Titantron (match / Season event screen): the skip button belongs to the game's cutscene, not to this movie\n"),
 ("svr06_movie_set_inworld(g_reqPtid >= 10 && g_reqPtid <= 14 && inMatch);",
  "s_titanInWorld = (g_reqPtid >= 10 && g_reqPtid <= 14 && inMatch); svr06_movie_set_inworld(s_titanInWorld);"),
 ("(!s_isTitle && skipEdge)",
  "(!s_isTitle && !s_titanInWorld && skipEdge)"),
]
E['music'] = [
 ("    std::mutex g_mtx;\n",
  "    std::mutex g_mtx;\n"
  "    // CL-0302: game ADXT handles whose clip finished and whose slot was then recycled. A later ADXT_GetStat (Season dialogue waits for\n"
  "    // PLAYEND) used to fall through to the original ADXT state, which never leaves STOP because the original stream never started.\n"
  "    std::vector<uint32_t> g_endedHandles; // guarded by g_mtx\n"
  "    void rememberEndedLocked(Slot &s)\n"
  "    {\n"
  "        const uint32_t h = s.handle.load();\n"
  "        if (h == 0u || h >= 0x2000000u || !s.ended.load()) return;\n"
  "        if (std::find(g_endedHandles.begin(), g_endedHandles.end(), h) == g_endedHandles.end())\n"
  "        {\n"
  "            if (g_endedHandles.size() >= 64u) g_endedHandles.erase(g_endedHandles.begin());\n"
  "            g_endedHandles.push_back(h);\n"
  "        }\n"
  "    }\n"),
 ("        s.started.store(false);\n        s.handle.store(0);\n    }\n",
  "        s.started.store(false);\n        s.handle.store(0);\n"
  "        s.oneshot.store(false); s.seqBusy.store(false); s.paused.store(false); s.played.store(0); s.gain.store(1.0f); s.cat.store(0); // CL-0302: a reused slot inherited 'paused' (silent clip) or 'oneshot' (game stream reaped -> GetStat lost)\n"
  "    }\n"),
 ("            if (s.active.load() && s.oneshot.load() && s.ended.load() && !s.seqBusy.load())\n                closeSlot(s);\n",
  "            if (s.active.load() && s.oneshot.load() && s.ended.load() && !s.seqBusy.load())\n                { rememberEndedLocked(s); closeSlot(s); }\n"),
 ("            if (c.ended.load()) { closeSlot(c); s = &c; break; }",
  "            if (c.ended.load()) { rememberEndedLocked(c); closeSlot(c); s = &c; break; }"),
 ("    svr06AudioLog(\"music\", rel, entry, handle);\n    std::lock_guard<std::mutex> lk(g_mtx);\n",
  "    svr06AudioLog(\"music\", rel, entry, handle);\n    std::lock_guard<std::mutex> lk(g_mtx);\n"
  "    g_endedHandles.erase(std::remove(g_endedHandles.begin(), g_endedHandles.end(), handle), g_endedHandles.end()); // CL-0302\n"),
 ("    s->handle.store(handle);\n    s->active.store(true);\n    s->gain.store(1.0f);\n",
  "    s->handle.store(handle);\n    s->active.store(true);\n    s->gain.store(1.0f);\n"
  "    s->oneshot.store(false); s->seqBusy.store(false); s->ended.store(false); // CL-0302: game streams are never self-freeing one-shots\n"),
 ("    if (!s)\n        return -1;\n    return s->ended.load() ? 5 : 3;\n",
  "    if (!s)\n    {\n        std::lock_guard<std::mutex> lk(g_mtx);\n"
  "        return std::find(g_endedHandles.begin(), g_endedHandles.end(), handle) != g_endedHandles.end() ? 5 : -1; // CL-0302\n"
  "    }\n    return s->ended.load() ? 5 : 3;\n"),
 ("    if (!s)\n        return false;\n    closeSlot(*s);\n    return true;\n",
  "    if (!s)\n    {\n"
  "        auto it = std::find(g_endedHandles.begin(), g_endedHandles.end(), handle); // CL-0302: stop of a finished, recycled handle\n"
  "        if (it == g_endedHandles.end()) return false;\n"
  "        g_endedHandles.erase(it);\n        return true;\n    }\n    closeSlot(*s);\n    return true;\n"),
]
E['movie'] = [
 ("    std::atomic<bool> g_titleMovie{false};\n",
  "    std::atomic<bool> g_titleMovie{false};\n    std::atomic<bool> g_inWorld{false}; // CL-0302: moved up so the decoder can see it\n"),
 ("static std::atomic<bool> g_inWorld{false};\n", ""),
 ("                        if (n > 0)\n                        {\n                            for (int tries = 0;",
  "                        if (n > 0 && !g_inWorld.load() && !g_vramFeed.load()) // CL-0302: no consumer for in-world / VRAM-fed movie audio: drop it instead of stalling the decoder 200 ms per packet\n                        {\n                            for (int tries = 0;"),
]
E['rpc'] = [
 ("            if (iopResult.signalCompletion)\n",
  "            else if (iopResult.signalCompletion) // CL-0302: a NOWAIT call signalled the completion semaphore twice\n"),
]
E['spu'] = [
 ("            if (isSweep(v))\n                return 1.0f;\n",
  "            if (isSweep(v))\n                return (v & 0x2000u) ? 0.0f : 1.0f; // CL-0302: sweeps are not modelled; a decreasing sweep (fade-out) now ends silent instead of full volume\n"),
 ("            unsigned konBad = 0, runaway = 0;\n",
  "            unsigned konBad = 0, runaway = 0;\n            unsigned koffs = 0, toneGuard = 0; // CL-0302\n"),
 ("                if ((value >> i) & 1 && base + i < kVoices && c.v[base + i].on)\n                    c.v[base + i].phase = Phase::Release;\n",
  "                if ((value >> i) & 1 && base + i < kVoices && c.v[base + i].on)\n                { c.v[base + i].phase = Phase::Release; ++g.koffs; }\n"),
 ("                    vo.blockAddr = vo.nextLoop;\n                    vo.walk = 0;\n",
  "                    {   // CL-0302 tone guard: a loop of at most 4 ADPCM blocks (<= 112 samples) still sounding 4 s after key-on is a stuck\n"
  "                        // tone loop (the 'ringing' that stacks with every hit). Real sample data never rings like that: release it.\n"
  "                        static const bool s_noGuard = std::getenv(\"SVR06_SPU2_NOTONEGUARD\") != nullptr;\n"
  "                        const uint32_t endA = vo.blockAddr & (kRamBytes - 1u);\n"
  "                        const uint32_t loopA = vo.nextLoop & (kRamBytes - 1u);\n"
  "                        if (!s_noGuard && vo.phase != Phase::Release && endA >= loopA && endA - loopA < 64u && g.frames - vo.konFrame > 4u * 48000u)\n"
  "                        {\n"
  "                            int amp = 0;\n"
  "                            for (int k = 3; k < 31; ++k) amp = std::max(amp, std::abs(static_cast<int>(vo.buf[k])));\n"
  "                            if (amp > 64)\n"
  "                            {\n"
  "                                if (g.toneGuard++ < 60u)\n"
  "                                    std::fprintf(stderr, \"[spu2] TONEGUARD core=%d v=%d ssa=0x%x loop=0x%x..0x%x amp=%d age=%.1fs -> released\\n\", &c == &g.core[0] ? 0 : 1, idx, vo.ssa, loopA, endA, amp, static_cast<double>(g.frames - vo.konFrame) / 48000.0);\n"
  "                                if ((vo.adsr2 & 0x1Fu) >= 0x18u) { vo.on = false; vo.phase = Phase::Off; vo.env = 0; return; } // release would take seconds\n"
  "                                vo.phase = Phase::Release;\n"
  "                            }\n"
  "                        }\n"
  "                    }\n"
  "                    vo.blockAddr = vo.nextLoop;\n                    vo.walk = 0;\n"),
 ("static_cast<int>(lv.phase), lv.env, lv.loopSet ? 1 : 0, lv.lsa, lv.pitch, lv.volL, lv.volR, lv.adsr1, lv.adsr2);\n",
  "static_cast<int>(lv.phase), lv.env, lv.loopSet ? 1 : 0, lv.lsa, lv.pitch, lv.volL, lv.volR, lv.adsr1, lv.adsr2);\n"
  "                            {   // CL-0302: once per keyed voice: loop start, current block, loudness and the first loop bytes (is a ring a short tone loop?)\n"
  "                                static uint64_t s_dumped[2][kVoices] = {};\n"
  "                                if (s_dumped[ci][vi] != lv.konFrame + 1u)\n"
  "                                {\n"
  "                                    s_dumped[ci][vi] = lv.konFrame + 1u;\n"
  "                                    int amp = 0;\n"
  "                                    for (int k = 3; k < 31; ++k) amp = std::max(amp, std::abs(static_cast<int>(lv.buf[k])));\n"
  "                                    std::fprintf(stderr, \"[spu2] LOOPINFO core=%d v=%d ssa=0x%x loopStart=0x%x cur=0x%x amp=%d bytes:\", ci, vi, lv.ssa, lv.nextLoop, lv.blockAddr, amp);\n"
  "                                    for (uint32_t k = 0; k < 32u; ++k) std::fprintf(stderr, \"%02x\", g.ram[(lv.nextLoop + k) & (kRamBytes - 1u)]);\n"
  "                                    std::fprintf(stderr, \"\\n\");\n"
  "                                }\n"
  "                            }\n"),
 ("                        std::fprintf(stderr, \"[spu2] state core=%d attr=",
  "                        if (ci == 0) std::fprintf(stderr, \"[spu2] koffs=%u toneguard=%u\\n\", g.koffs, g.toneGuard); // CL-0302\n"
  "                        std::fprintf(stderr, \"[spu2] state core=%d attr="),
]
ok_all = True
for key, rel in P.items():
    path = os.path.join(root, rel)
    if not os.path.exists(path):
        print('MISSING', path); ok_all = False; continue
    raw = open(path, 'rb').read().decode('utf8')
    crlf = '\r\n' in raw
    t = raw.replace('\r\n', '\n')
    if 'CL-0302' in t:
        print('already', rel); continue
    bad = [a[:60] for a, b in E[key] if t.count(a) != 1]
    if bad:
        print('SKIP', rel, 'anchors:', bad); ok_all = False; continue
    for a, b in E[key]:
        t = t.replace(a, b)
    if crlf: t = t.replace('\n', '\r\n')
    shutil.copy2(path, path + '.bak_cl0302')
    open(path, 'wb').write(t.encode('utf8'))
    print('patched', rel, 'crlf' if crlf else 'lf', 'braces', t.count('{') - t.count('}'))
print('DONE' if ok_all else 'DONE WITH SKIPS')
