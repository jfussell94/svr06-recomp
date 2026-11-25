// CL-0390: native drop-down menu bar on the game window (System / View / Debug / Help), like PCSX2 and most desktop apps, and
// a native debugger window (Debug > Open Debugger, or F2). The debugger window runs its own thread and message loop, so it keeps
// working while a breakpoint holds the game thread. Commands are the debugger commands of SvrDebugger.cpp plus every live
// console command (ee, poke32, watchw, ramdump ... the same set as tools/svrlive.py). SVR06_NOMENUBAR=1 turns the bar off.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace svrdbg
{
bool consoleCommand(uint8_t *rdram, const std::string &c, std::istream &ls, std::ostream &out);
std::string bpList();
std::string threadsText();
std::string regsText();
std::vector<std::string> logTail(size_t n);
std::string bpDel(uint32_t addr);
bool isPaused();
void resume();
}

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
std::atomic<int> g_cmd{0};
std::atomic<uint8_t *> g_rdram{nullptr};
WNDPROC g_oldProc = nullptr;
HWND g_game = nullptr;
std::atomic<HWND> g_dbgWnd{nullptr};
std::atomic<bool> g_dbgThread{false};
HWND g_out = nullptr, g_in = nullptr;
HFONT g_font = nullptr;
const char *kLive = "analysis/live/";

enum : int
{
    ID_EXIT = 1009, ID_FULL = 1005, ID_FPS = 1006, ID_DBG = 1002, ID_CONT = 1007, ID_BPCLR = 1008, ID_SHOT = 1010, ID_RAM = 1011, ID_VRAM = 1012, ID_HELP = 1020, ID_SS1 = 1021, ID_SS2, ID_SS3, ID_SS4, ID_SL1 = 1031, ID_SL2, ID_SL3, ID_SL4,
    ID_RUN = 2001, ID_B_BPL = 2002, ID_B_TH = 2003, ID_B_CONT = 2004, ID_B_REGS = 2005, ID_B_LOG = 2006, ID_B_HELP = 2007, ID_B_CLR = 2008
};

void liveCmd(const std::string &c)
{
    std::error_code ec;
    std::filesystem::create_directories(kLive, ec);
    std::ofstream f(std::string(kLive) + "cmd.txt", std::ios::app);
    f << c << "\n";
}

LRESULT CALLBACK gameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && HIWORD(wp) == 0 && LOWORD(wp) >= 1000 && LOWORD(wp) < 1100)
    {
        g_cmd.store(LOWORD(wp));
        return 0;
    }
    return CallWindowProcW(g_oldProc, h, msg, wp, lp);
}

BOOL CALLBACK findGame(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr || h == g_dbgWnd.load()) return TRUE;
    RECT r{};
    GetWindowRect(h, &r);
    if ((r.right - r.left) * (r.bottom - r.top) > 200 * 150) { *reinterpret_cast<HWND *>(lp) = h; return FALSE; }
    return TRUE;
}

void appendOut(const std::string &text)
{
    if (!g_out) return;
    std::string t;
    for (char ch : text) { if (ch == '\n') t += "\r\n"; else t += ch; }
    const int len = GetWindowTextLengthA(g_out);
    if (len > 400000) { SetWindowTextA(g_out, ""); }
    const int n = GetWindowTextLengthA(g_out);
    SendMessageA(g_out, EM_SETSEL, n, n);
    SendMessageA(g_out, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(t.c_str()));
}

std::string helpText()
{
    return "Debugger commands (type in the box, Enter):\n"
           "  bp ADDR [aN=HEX]   log every call of the function at ADDR (caller, args, return, call stack)\n"
           "  bpp ADDR           pause the game thread when ADDR is called (Continue / F5 resumes)\n"
           "  bpc ADDR           count calls     bpd ADDR | bpd all     bpl  list breakpoints + last hits\n"
           "  regs               registers of the paused thread       cont  resume\n"
           "  dis ADDR [N]       disassemble (* = function entry, breakpoints go there)\n"
           "  th                 threads with call stacks             bt SP  call stack from a stack pointer\n"
           "  wp ADDR [LEN]      catch the code that WRITES ADDR (wpr: reads too, LEN 0 disarms) -> analysis/live/pagew.txt\n"
           "  find u8|u16|u32|f32 VAL [LO HI] | find str TEXT | find hex BYTES    findnext eq|ne|inc|dec|changed|same [VAL]\n"
           "Live console commands also work here: ee ADDR LEN, ee32 ADDR N, poke32 ADDR VAL, ramdump NAME, vramdump, shot NAME, threads, frame ...\n"
           "Limits: native build, so no single-instruction stepping; breakpoints sit on function entries.\n";
}

void runLine(const std::string &line)
{
    appendOut("> " + line + "\n");
    std::istringstream ls(line);
    std::string c;
    ls >> c;
    if (c.empty()) return;
    if (c == "help" || c == "?") { appendOut(helpText()); return; }
    std::ostringstream out;
    if (svrdbg::consoleCommand(g_rdram.load(), c, ls, out)) { appendOut(out.str()); return; }
    // any other line goes to the live console thread; show what it appends to out.txt
    const std::string outPath = std::string(kLive) + "out.txt";
    std::error_code ec;
    const auto before = std::filesystem::exists(outPath, ec) ? std::filesystem::file_size(outPath, ec) : 0;
    liveCmd(line);
    for (int i = 0; i < 40; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto now = std::filesystem::exists(outPath, ec) ? std::filesystem::file_size(outPath, ec) : 0;
        if (now > before && !std::filesystem::exists(std::string(kLive) + "cmd.txt", ec))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::ifstream f(outPath, std::ios::binary);
            f.seekg(static_cast<std::streamoff>(before));
            std::stringstream ss;
            ss << f.rdbuf();
            appendOut(ss.str());
            return;
        }
    }
    appendOut("(no answer from the live console yet; see analysis/live/out.txt)\n");
}

WNDPROC g_inOld = nullptr;
LRESULT CALLBACK inProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { PostMessageW(GetParent(h), WM_COMMAND, ID_RUN, 0); return 0; }
    if (msg == WM_CHAR && wp == '\r') return 0;
    return CallWindowProcW(g_inOld, h, msg, wp, lp);
}

void layout(HWND h)
{
    RECT r{};
    GetClientRect(h, &r);
    const int W = r.right, H = r.bottom, bh = 26, pad = 6;
    int x = pad;
    const int ids[] = {ID_B_BPL, ID_B_TH, ID_B_REGS, ID_B_LOG, ID_B_CONT, ID_B_CLR, ID_B_HELP};
    for (int id : ids) { MoveWindow(GetDlgItem(h, id), x, pad, 92, bh, TRUE); x += 96; }
    MoveWindow(g_out, pad, pad * 2 + bh, W - pad * 2, H - (pad * 4 + bh * 2), TRUE);
    MoveWindow(g_in, pad, H - pad - bh, W - pad * 3 - 70, bh, TRUE);
    MoveWindow(GetDlgItem(h, ID_RUN), W - pad - 70, H - pad - bh, 70, bh, TRUE);
}

LRESULT CALLBACK dbgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        auto btn = [h](const char *t, int id) { HWND b = CreateWindowA("BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, h, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr); SendMessageA(b, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE); };
        btn("Breakpoints", ID_B_BPL); btn("Threads", ID_B_TH); btn("Registers", ID_B_REGS); btn("Hit log", ID_B_LOG); btn("Continue F5", ID_B_CONT); btn("Clear", ID_B_CLR); btn("Help", ID_B_HELP);
        g_out = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY | WS_BORDER, 0, 0, 10, 10, h, nullptr, nullptr, nullptr);
        SendMessageA(g_out, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
        g_in = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, h, nullptr, nullptr, nullptr);
        g_inOld = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_in, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(inProc)));
        btn("Run", ID_RUN);
        SendMessageA(g_out, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        SendMessageA(g_in, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
        appendOut(helpText());
        return 0;
    }
    case WM_SIZE: layout(h); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case ID_RUN:
        {
            char b[512] = {0};
            GetWindowTextA(g_in, b, sizeof b - 1);
            SetWindowTextA(g_in, "");
            const std::string line = b;
            std::thread([line] { runLine(line); }).detach(); // keep the window responsive while the console answers
            return 0;
        }
        case ID_B_BPL: appendOut(svrdbg::bpList()); return 0;
        case ID_B_TH: appendOut(svrdbg::threadsText()); return 0;
        case ID_B_REGS: appendOut(svrdbg::regsText()); return 0;
        case ID_B_LOG: { std::string s; for (const auto &l : svrdbg::logTail(60)) s += l + "\n"; appendOut(s.empty() ? std::string("no hits yet\n") : s); return 0; }
        case ID_B_CONT: appendOut(svrdbg::isPaused() ? "resumed\n" : "not paused\n"); svrdbg::resume(); return 0;
        case ID_B_CLR: SetWindowTextA(g_out, ""); return 0;
        case ID_B_HELP: appendOut(helpText()); return 0;
        default: break;
        }
        break;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    default: break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

void dbgThread()
{
    g_font = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    WNDCLASSA wc{};
    wc.lpfnWndProc = dbgProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SvR06Debugger";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    HWND h = CreateWindowA("SvR06Debugger", "SvR06 Debugger", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, nullptr, nullptr, wc.hInstance, nullptr);
    g_dbgWnd.store(h);
    ShowWindow(h, SW_SHOW);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageA(&m); }
}

void openDebugger()
{
    if (!g_dbgThread.exchange(true)) { std::thread(dbgThread).detach(); return; }
    if (HWND h = g_dbgWnd.load()) { ShowWindow(h, IsWindowVisible(h) ? SW_HIDE : SW_SHOW); if (IsWindowVisible(h)) SetForegroundWindow(h); }
}

bool installMenu(HWND h)
{
    HMENU sys = CreatePopupMenu();
    AppendMenuW(sys, MF_STRING, ID_EXIT, L"Exit");
    HMENU dbg = CreatePopupMenu();
    HMENU ss = CreatePopupMenu();
    AppendMenuW(ss, MF_STRING, ID_SS1, L"Save state 1\tF6"); AppendMenuW(ss, MF_STRING, ID_SS2, L"Save state 2"); AppendMenuW(ss, MF_STRING, ID_SS3, L"Save state 3"); AppendMenuW(ss, MF_STRING, ID_SS4, L"Save state 4");
    AppendMenuW(ss, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(ss, MF_STRING, ID_SL1, L"Load state 1\tF7"); AppendMenuW(ss, MF_STRING, ID_SL2, L"Load state 2"); AppendMenuW(ss, MF_STRING, ID_SL3, L"Load state 3"); AppendMenuW(ss, MF_STRING, ID_SL4, L"Load state 4");
    AppendMenuW(dbg, MF_STRING, ID_DBG, L"Open Debugger\tF2");
    AppendMenuW(dbg, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(dbg, MF_STRING, ID_CONT, L"Continue (resume paused game)\tF5");
    AppendMenuW(dbg, MF_STRING, ID_BPCLR, L"Remove all breakpoints");
    AppendMenuW(dbg, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(dbg, MF_STRING, ID_SHOT, L"Screenshot (analysis\\live\\shots)");
    AppendMenuW(dbg, MF_STRING, ID_RAM, L"Dump RAM (analysis\\live\\live_ram.bin)");
    AppendMenuW(dbg, MF_STRING, ID_VRAM, L"Dump GS VRAM (analysis\\live\\live_vram.bin)");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_HELP, L"Debugger help");
    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sys), L"System");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(ss), L"Save states");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(dbg), L"Debug");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"Help");
    if (!SetMenu(h, bar)) { DestroyMenu(bar); return false; }
    g_oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(h, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gameProc)));
    DrawMenuBar(h);
    return true;
}
} // namespace

// Called once per presented frame (from svr06_console_poll).
void svr06SsRequest(int op, int slot); // CL-0392 SvrSavestate.cpp
void svr06MenuTick(uint8_t *rdram)
{
    {   // CL-0391: frame hitch log (frames over 26 ms) -> analysis/live/hitch.txt, with a running frame number and a wall-clock stamp
        static auto s_last = std::chrono::steady_clock::now();
        static unsigned long long s_n = 0;
        static std::ofstream s_f;
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - s_last).count();
        s_last = now; ++s_n;
        if (ms > 26.0 && s_n > 120)
        {
            if (!s_f.is_open()) { std::error_code ec; std::filesystem::create_directories("analysis/live", ec); s_f.open("analysis/live/hitch.txt", std::ios::app); }
            s_f << "frame " << s_n << " ms=" << ms << " t=" << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() << "\n";
            s_f.flush();
        }
    }
    g_rdram.store(rdram);
    static bool s_off = std::getenv("SVR06_NOMENUBAR") != nullptr;
    static int s_tries = 0;
    if (!s_off && !g_game && s_tries < 600 && (++s_tries % 30) == 0)
    {
        HWND h = nullptr;
        EnumWindows(findGame, reinterpret_cast<LPARAM>(&h));
        if (h && installMenu(h)) { g_game = h; std::fprintf(stderr, "[menubar] installed on the game window\n"); }
    }
    // hotkeys while the game window has focus: F2 debugger, F5 continue
    static bool s_f2 = false, s_f5 = false;
    const HWND fg = GetForegroundWindow();
    const bool ours = fg && (fg == g_game || fg == g_dbgWnd.load());
    const bool f2 = ours && (GetAsyncKeyState(VK_F2) & 0x8000) != 0, f5 = ours && (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f2 && !s_f2) openDebugger();
    if (f5 && !s_f5 && svrdbg::isPaused()) svrdbg::resume();
    { static bool s_f6 = false, s_f7 = false; const bool f6 = ours && (GetAsyncKeyState(VK_F6) & 0x8000) != 0, f7 = ours && (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
      if (f6 && !s_f6) svr06SsRequest(1, 0); if (f7 && !s_f7) svr06SsRequest(2, 0); s_f6 = f6; s_f7 = f7; }
    s_f2 = f2; s_f5 = f5;
    const int g_lastCmd = g_cmd.exchange(0);
    switch (g_lastCmd)
    {
    case ID_EXIT: liveCmd("quit"); break;
    case ID_DBG: openDebugger(); break;
    case ID_CONT: svrdbg::resume(); break;
    case ID_BPCLR: svrdbg::bpDel(0); break;
    case ID_SHOT: liveCmd("shot"); break;
    case ID_RAM: liveCmd("ramdump"); break;
    case ID_VRAM: liveCmd("vramdump"); break;
    case ID_HELP: openDebugger(); break;
    case ID_SS1: case ID_SS2: case ID_SS3: case ID_SS4: svr06SsRequest(1, g_lastCmd - ID_SS1); break;
    case ID_SL1: case ID_SL2: case ID_SL3: case ID_SL4: svr06SsRequest(2, g_lastCmd - ID_SL1); break;
    default: break;
    }
}
#else
void svr06MenuTick(uint8_t *) {}
#endif
