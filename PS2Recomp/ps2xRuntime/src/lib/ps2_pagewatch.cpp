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
#else
void svr06HwWatchHost(uint8_t *, uint32_t, uint32_t) {}
void svr06StartPageWatch(uint8_t *) {}
void svr06PageWatchSet(uint8_t *, uint32_t, uint32_t) {}
#endif
