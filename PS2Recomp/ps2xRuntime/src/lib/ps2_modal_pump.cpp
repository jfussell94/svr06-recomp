// CL-0297: keep the presenter alive while Windows runs its modal move/size loop (holding or dragging the title bar).
// The loop blocks the main thread (which owns the window and drives the GS scan-out hand-off), so the game thread and the
// GS worker ended up waiting on a presenter that never ran. A window-proc subclass starts a short timer for the duration
// of the modal loop and calls the host's pump (frame hand-off only, no drawing) from WM_TIMER.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace
{
    WNDPROC g_old = nullptr;
    void (*g_pump)() = nullptr;
    bool g_inPump = false;
    bool g_timer = false;
    constexpr UINT_PTR kTimerId = 0x5606;

    LRESULT CALLBACK pumpProc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        switch (m)
        {
        case WM_ENTERSIZEMOVE:
            if (!g_timer) { SetTimer(h, kTimerId, 8, nullptr); g_timer = true; }
            break;
        case WM_NCLBUTTONDOWN:   // a click-and-hold on the caption enters the modal loop before any movement
            if (!g_timer && (w == HTCAPTION || w == HTLEFT || w == HTRIGHT || w == HTTOP || w == HTBOTTOM)) { SetTimer(h, kTimerId, 8, nullptr); g_timer = true; }
            break;
        case WM_EXITSIZEMOVE:
        case WM_CAPTURECHANGED:
            if (g_timer) { KillTimer(h, kTimerId); g_timer = false; }
            break;
        case WM_TIMER:
            if (w == kTimerId)
            {
                if (g_pump && !g_inPump) { g_inPump = true; g_pump(); g_inPump = false; }
                return 0;
            }
            break;
        default: break;
        }
        return CallWindowProcW(g_old, h, m, w, l);
    }
}

void svr06InstallModalPump(void *hwnd, void (*pump)())
{
    if (!hwnd || g_old) return;
    g_pump = pump;
    g_old = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(static_cast<HWND>(hwnd), GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&pumpProc)));
}
#else
void svr06InstallModalPump(void *, void (*)()) {}
#endif
