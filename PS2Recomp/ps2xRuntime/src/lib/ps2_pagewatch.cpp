#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
// ---- ANALYSIS AID [pagewatch] (remove before shipping): trap host writes to the RDRAM page holding the garbage view-matrix packet ----
static uint8_t *g_pwPage = nullptr;
static uint32_t g_pwLo = 0, g_pwHi = 0;
static volatile LONG g_pwLogged = 0;
static FILE *g_pwOut = nullptr;
uint32_t svr06HostToGuest(uintptr_t host);
void svr06BuildHostFnMap();
static uint32_t g_pwBaseAddr = 0;
static volatile LONG g_pwArmed = 0;
static volatile LONG g_hwArmed = 0;
static LONG CALLBACK svr06PageWatchHandler(PEXCEPTION_POINTERS ep)
{
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_SINGLE_STEP && (ep->ContextRecord->Dr6 & 0xF) && g_hwArmed)
    {
        if (g_pwLogged < 1500)
        {
            InterlockedIncrement(&g_pwLogged);
            void *frames[16];
            USHORT n = CaptureStackBackTrace(0, 16, frames, nullptr);
            std::fprintf(g_pwOut, "[hwbp] dr6=%llx tid=%lu rip=%p guest~0x%08x frames=%u\n", (unsigned long long)ep->ContextRecord->Dr6, GetCurrentThreadId(), (void *)ep->ContextRecord->Rip, svr06HostToGuest((uintptr_t)ep->ContextRecord->Rip), (unsigned)n);
            { const DWORD64 drs[4] = {ep->ContextRecord->Dr0, ep->ContextRecord->Dr1, ep->ContextRecord->Dr2, ep->ContextRecord->Dr3};
              for (int q = 0; q < 4; ++q) if ((ep->ContextRecord->Dr6 >> q) & 1) { const float *fp = (const float *)(uintptr_t)drs[q]; std::fprintf(g_pwOut, "[hwbp] val dr%d host=%p u32=%08x %08x f=%g %g\n", q, (void *)(uintptr_t)drs[q], ((const uint32_t *)fp)[0], ((const uint32_t *)fp)[1], fp[0], fp[1]); } }
            { // [ctxpc] find the R5900Context among host registers and print its guest pc/ra/sp (remove before shipping)
              const DWORD64 rg[] = {ep->ContextRecord->Rbx, ep->ContextRecord->Rsi, ep->ContextRecord->Rdi, ep->ContextRecord->Rbp, ep->ContextRecord->R12, ep->ContextRecord->R13, ep->ContextRecord->R14, ep->ContextRecord->R15, ep->ContextRecord->Rcx, ep->ContextRecord->Rdx, ep->ContextRecord->R8, ep->ContextRecord->R9};
              for (DWORD64 v : rg) {
                  if (!v || (v & 15)) continue;
                  MEMORY_BASIC_INFORMATION mbi;
                  if (!VirtualQuery((void *)v, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) continue;
                  if ((uintptr_t)v + 0x240 > (uintptr_t)mbi.BaseAddress + mbi.RegionSize) continue;
                  const uint32_t pc = *(const uint32_t *)(v + 512), ra = *(const uint32_t *)(v + 31 * 16), sp = *(const uint32_t *)(v + 29 * 16);
                  if (pc >= 0x100000 && pc < 0x1000000 && sp >= 0x100000 && sp < 0x2000000)
                      std::fprintf(g_pwOut, "[hwbp] ctx=%p pc=%08x ra=%08x sp=%08x\n", (void *)v, pc, ra, sp);
              }
            }
            for (USHORT i = 1; i < n; ++i)
                std::fprintf(g_pwOut, "[hwbp]   #%u guest~0x%08x\n", (unsigned)i, svr06HostToGuest((uintptr_t)frames[i]));
            std::fflush(g_pwOut);
        }
        ep->ContextRecord->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && g_pwPage && g_pwArmed)
    {
        DWORD old;
        VirtualProtect(g_pwPage, 0x1000, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 && g_pwPage && g_pwArmed)
    {
        const uint8_t *fa = (const uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
        if (fa >= g_pwPage && fa < g_pwPage + 0x1000)
        {
            const uint32_t off = (uint32_t)(fa - g_pwPage);
            if (ep->ExceptionRecord->ExceptionInformation[0] == 1 && off >= g_pwLo && off < g_pwHi && g_pwLogged < 150)
            {
                InterlockedIncrement(&g_pwLogged);
                void *frames[16];
                USHORT n = CaptureStackBackTrace(0, 16, frames, nullptr);
                std::fprintf(g_pwOut ? g_pwOut : stderr, "[pagew] write rdram 0x%x tid=%lu frames=%u\n", g_pwBaseAddr + off, GetCurrentThreadId(), (unsigned)n);
                for (USHORT i = 1; i < n; ++i)
                    std::fprintf(g_pwOut ? g_pwOut : stderr, "[pagew]   #%u host=%p guest~0x%08x\n", (unsigned)i, frames[i], svr06HostToGuest((uintptr_t)frames[i]));
            }
            if (g_pwOut) std::fflush(g_pwOut);
            DWORD old;
            VirtualProtect(g_pwPage, 0x1000, PAGE_READWRITE, &old);
            ep->ContextRecord->EFlags |= 0x100; // single-step, then re-protect
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void svr06StartPageWatch(uint8_t *rdram)
{
    if (!std::getenv("SVR06_PAGEWATCH"))
        return;
    g_pwPage = rdram + 0x1CF9000u;
    g_pwBaseAddr = 0x1CF9000u;
    g_pwArmed = 1;
    g_pwLo = 0x7D0u;
    g_pwHi = 0x890u;
    const char *d = std::getenv("SVR06_PAGEWATCH_DELAY");
    const int delaySec = d ? std::atoi(d) : 60;
    AddVectoredExceptionHandler(1, svr06PageWatchHandler);
    std::thread([delaySec]() {
        std::this_thread::sleep_for(std::chrono::seconds(delaySec));
        DWORD old;
        VirtualProtect(g_pwPage, 0x1000, PAGE_READONLY, &old);
        std::fprintf(stderr, "[pagew] armed on rdram page 0x1cf9000 after %d s\n", delaySec);
    }).detach();
}
// runtime control from the live console: arm a write trap on [addr, addr+len) (must fit in one 4 KB page); len==0 disarms
static int g_pwRwMode = 0; // ISS-137: 0 = break on writes, 1 = break on reads or writes
void svr06HwWatchRwMode(int readsToo) { g_pwRwMode = readsToo ? 1 : 0; }
void svr06HwWatchHost(uint8_t *hostBase, uint32_t addr, uint32_t len)
{
    uint8_t *rdram = hostBase;
    static bool s_handler = false;
    if (!s_handler) { s_handler = true; AddVectoredExceptionHandler(1, svr06PageWatchHandler); }
    svr06BuildHostFnMap();
    if (!g_pwOut) g_pwOut = std::fopen("analysis/live/pagew.txt", "a");
    // hardware data write breakpoints (DR0-DR3, 8 bytes each) on every thread: no page protection, no I/O side effects
    uint64_t dr[4] = {0, 0, 0, 0};
    int nreg = 0;
    if (len)
    {
        uint32_t a0 = addr & ~7u;
        for (uint32_t a2 = a0; a2 < addr + len && nreg < 4; a2 += 8) dr[nreg++] = (uint64_t)(uintptr_t)(rdram + a2);
        g_pwLogged = 0;
        g_hwArmed = 1;
    }
    else g_hwArmed = 0;
    uint64_t dr7 = 0;
    // ISS-137: RW bits 01 = write only, 11 = read **or** write. Reads matter when the question is which guest code
    // READS a table (e.g. who looks up the ring-announcer entry), not who writes it.
    const uint64_t rw = g_pwRwMode ? 3ull : 1ull;
    for (int i = 0; i < nreg; ++i) dr7 |= (1ull << (i * 2)) | (rw << (16 + i * 4)) | (3ull << (18 + i * 4)) /*8 bytes*/;
    const DWORD me = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    int done = 0;
    if (snap != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 te; te.dwSize = sizeof(te);
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        {
            if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == me) continue;
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
            if (!h) continue;
            if (SuspendThread(h) != (DWORD)-1)
            {
                CONTEXT c; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (GetThreadContext(h, &c))
                {
                    c.Dr0 = dr[0]; c.Dr1 = dr[1]; c.Dr2 = dr[2]; c.Dr3 = dr[3]; c.Dr6 = 0; c.Dr7 = dr7;
                    if (SetThreadContext(h, &c)) ++done;
                }
                ResumeThread(h);
            }
            CloseHandle(h);
        }
        CloseHandle(snap);
    }
    std::fprintf(g_pwOut, "[hwbp] %s base=%p addr=0x%x len=%u regs=%d threads=%d\n", len ? "armed" : "disarmed", (void *)hostBase, addr, len, nreg, done);
    std::fflush(g_pwOut);
}
void svr06PageWatchSet(uint8_t *rdram, uint32_t addr, uint32_t len) { svr06HwWatchHost(rdram, addr, len); }
// CL-0310b live EE stack sampler support (used by the prof.req sampler in ps2_runtime.cpp). No allocation while the target is suspended.
uint32_t svr06CurOsTid() { return (uint32_t)GetCurrentThreadId(); }
uint64_t svr06ExeBase() { return (uint64_t)(uintptr_t)GetModuleHandleW(nullptr); }
// CL-0410: module name + offset of a host address (resolves RIPs that lie outside the exe: drivers, ntdll, ...)
int svr06RipModule(uint64_t rip, char *out, int n, uint64_t *off)
{
    HMODULE m = nullptr; out[0] = 0; *off = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)rip, &m) || !m) return 0;
    char path[MAX_PATH]; if (!GetModuleFileNameA(m, path, MAX_PATH)) return 0;
    const char *b = path; for (const char *q = path; *q; ++q) if (*q == '\\' || *q == '/') b = q + 1;
    std::snprintf(out, (size_t)n, "%s", b); *off = rip - (uint64_t)(uintptr_t)m; return 1;
}
int svr06SampleThreadStack(uint32_t osTid, uint64_t *rips, int maxFrames)
{
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, (DWORD)osTid);
    if (!h) return 0;
    int n = 0;
    if (SuspendThread(h) != (DWORD)-1)
    {
        CONTEXT c; ZeroMemory(&c, sizeof c); c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(h, &c))
        {
            while (n < maxFrames && c.Rip != 0)
            {
                rips[n++] = c.Rip;
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
                if (!rf) { if (c.Rsp == 0) break; c.Rip = *(DWORD64 *)c.Rsp; c.Rsp += 8; }
                else { PVOID hd = nullptr; DWORD64 ef = 0; RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, rf, &c, &hd, &ef, nullptr); }
            }
        }
        ResumeThread(h);
    }
    CloseHandle(h);
    return n;
}

// ---- CL-0314 crash reporter: one small file analysis/live/crash.txt, written only when the process dies (no per-frame cost) ----
#include <csignal>
#include <exception>
#include "svr06_prof.h"
static void svr06WriteCrash(const char *why, EXCEPTION_POINTERS *ep)
{
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return;
    FILE *f = std::fopen("analysis/live/crash.txt", "w");
    if (!f) return;
    const unsigned char *base = (const unsigned char *)GetModuleHandleA(nullptr);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    const unsigned long long size = nt->OptionalHeader.SizeOfImage;
    std::fprintf(f, "why=%s hosttid=%lu\n", why, (unsigned long)GetCurrentThreadId());
    std::fprintf(f, "guest pc=%X eeTid=%u zone=%d eeHostTid=%u\n", (unsigned)svr06prof::g_pc.load(), (unsigned)svr06prof::g_tid.load(), (int)svr06prof::g_zone.load(), (unsigned)svr06prof::g_eeOsTid.load());
    if (ep && ep->ExceptionRecord && ep->ContextRecord)
    {
        const EXCEPTION_RECORD *er = ep->ExceptionRecord; const CONTEXT *c = ep->ContextRecord;
        std::fprintf(f, "code=%08lX flags=%lX rip=exe+%llX (%p)\n", (unsigned long)er->ExceptionCode, (unsigned long)er->ExceptionFlags, (unsigned long long)((const unsigned char *)er->ExceptionAddress - base), er->ExceptionAddress);
        if (er->NumberParameters >= 2) std::fprintf(f, "param0=%llu (0 read,1 write,8 exec) param1=%p\n", (unsigned long long)er->ExceptionInformation[0], (void *)er->ExceptionInformation[1]);
        std::fprintf(f, "rax=%llX rbx=%llX rcx=%llX rdx=%llX rsi=%llX rdi=%llX rbp=%llX rsp=%llX\n", (unsigned long long)c->Rax, (unsigned long long)c->Rbx, (unsigned long long)c->Rcx, (unsigned long long)c->Rdx, (unsigned long long)c->Rsi, (unsigned long long)c->Rdi, (unsigned long long)c->Rbp, (unsigned long long)c->Rsp);
        std::fprintf(f, "r8=%llX r9=%llX r10=%llX r11=%llX r12=%llX r13=%llX r14=%llX r15=%llX\n", (unsigned long long)c->R8, (unsigned long long)c->R9, (unsigned long long)c->R10, (unsigned long long)c->R11, (unsigned long long)c->R12, (unsigned long long)c->R13, (unsigned long long)c->R14, (unsigned long long)c->R15);
        std::fprintf(f, "stack (return addresses inside the exe):");
        int shown = 0;
        for (int i = 0; i < 512 && shown < 48; ++i)
        {
            unsigned long long v = 0; SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (const void *)(c->Rsp + (unsigned long long)i * 8), &v, 8, &got) || got != 8) break;
            if (v >= (unsigned long long)base && v < (unsigned long long)base + size) { std::fprintf(f, " exe+%llX", v - (unsigned long long)base); ++shown; }
        }
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}
static LONG WINAPI svr06CrashFilter(EXCEPTION_POINTERS *ep) { svr06WriteCrash("unhandled-exception", ep); return EXCEPTION_CONTINUE_SEARCH; }
static void svr06TerminateHook() { svr06WriteCrash("std::terminate", nullptr); std::abort(); }
static void svr06AbortHook(int) { svr06WriteCrash("SIGABRT", nullptr); std::_Exit(3); }
static struct Svr06CrashInstall { Svr06CrashInstall() { SetUnhandledExceptionFilter(svr06CrashFilter); std::set_terminate(svr06TerminateHook); std::signal(SIGABRT, svr06AbortHook); } } s_svr06CrashInstall;

#else
uint32_t svr06CurOsTid() { return 0; }
uint64_t svr06ExeBase() { return 0; }
int svr06SampleThreadStack(uint32_t, uint64_t *, int) { return 0; }
int svr06RipModule(uint64_t, char *o, int, uint64_t *f) { o[0] = 0; *f = 0; return 0; }
void svr06HwWatchHost(uint8_t *, uint32_t, uint32_t) {}
void svr06StartPageWatch(uint8_t *) {}
void svr06PageWatchSet(uint8_t *, uint32_t, uint32_t) {}
#endif
