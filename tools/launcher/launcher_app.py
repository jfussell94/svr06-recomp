#!/usr/bin/env python3
"""SvR06 Launcher - native window edition (test build, 2026-11-14).

Same backend (server.py) and pages (ui/) as the Edge edition, shown in a native window (pywebview -> Microsoft WebView2,
part of Windows 10/11) with the launcher's own title bar instead of a browser window. Packaged by
scripts\\build_launcher_exe.cmd into SvR06Launcher.exe. Without pywebview installed it falls back to launcher.py (Edge)."""
import os, sys, threading, time

if not getattr(sys, 'frozen', False):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server

try:
    import webview  # pywebview
except Exception:
    webview = None


class Api:
    """Called from the page's title bar (window.pywebview.api.*)."""
    def __init__(self):
        self.win = None
        self.maxed = False
        self._saved = (100, 100, 1280, 820)

    def minimize(self):
        if self.win: self.win.minimize()

    def _hwnd(self):
        import ctypes
        return ctypes.windll.user32.FindWindowW(None, 'SvR06 Launcher')

    def _work_area(self, hwnd):
        """Work area (screen minus the taskbar) of the monitor the window is on, in physical pixels."""
        import ctypes
        from ctypes import wintypes
        class MI(ctypes.Structure):
            _fields_ = [('cbSize', wintypes.DWORD), ('rcMonitor', wintypes.RECT), ('rcWork', wintypes.RECT), ('dwFlags', wintypes.DWORD)]
        u = ctypes.windll.user32
        mon = u.MonitorFromWindow(wintypes.HWND(hwnd), 2)  # MONITOR_DEFAULTTONEAREST
        mi = MI(); mi.cbSize = ctypes.sizeof(MI)
        if not u.GetMonitorInfoW(mon, ctypes.byref(mi)): return None
        r = mi.rcWork
        return r.left, r.top, r.right - r.left, r.bottom - r.top

    def toggle_max(self):
        """CL-0254: a frameless pywebview window maximizes over the whole screen, hiding the Windows taskbar. Size it to the
        monitor's work area instead (screen minus taskbar) and restore the previous rectangle on the second click."""
        if not self.win: return False
        if sys.platform == 'win32':
            try:
                import ctypes
                from ctypes import wintypes
                u = ctypes.windll.user32
                hwnd = self._hwnd()
                if hwnd:
                    if self.maxed:
                        x, y, w, h = self._saved
                        u.SetWindowPos(hwnd, 0, x, y, w, h, 0x0004 | 0x0010)  # SWP_NOZORDER | SWP_NOACTIVATE
                        self.maxed = False
                    else:
                        r = wintypes.RECT(); u.GetWindowRect(hwnd, ctypes.byref(r))
                        self._saved = (r.left, r.top, r.right - r.left, r.bottom - r.top)
                        wa = self._work_area(hwnd)
                        if wa:
                            u.SetWindowPos(hwnd, 0, wa[0], wa[1], wa[2], wa[3], 0x0004 | 0x0010)
                            self.maxed = True
                    return self.maxed
            except Exception:
                pass
        if self.maxed: self.win.restore()
        else: self.win.maximize()
        self.maxed = not self.maxed
        return self.maxed

    def get_bounds(self):
        """CL-0260: window rectangle in physical pixels (x, y, w, h) for the page's edge resize handles."""
        if sys.platform == 'win32':
            try:
                import ctypes
                from ctypes import wintypes
                hwnd = self._hwnd()
                if hwnd:
                    r = wintypes.RECT(); ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(r))
                    return [r.left, r.top, r.right - r.left, r.bottom - r.top]
            except Exception:
                pass
        if self.win:
            return [self.win.x, self.win.y, self.win.width, self.win.height]
        return None

    def set_bounds(self, x, y, w, h):
        """CL-0260: borderless window resize (called while an edge/corner handle is dragged)."""
        if self.maxed: return False
        if sys.platform == 'win32':
            try:
                import ctypes
                hwnd = self._hwnd()
                if hwnd:
                    ctypes.windll.user32.SetWindowPos(hwnd, 0, int(x), int(y), int(w), int(h), 0x0004 | 0x0010)  # SWP_NOZORDER | SWP_NOACTIVATE
                    return True
            except Exception:
                pass
        if self.win:
            self.win.move(int(x), int(y)); self.win.resize(int(w), int(h))
        return True

    def style_window(self):
        """CL-0260: Windows 11 look for the borderless window - rounded corners and the system drop shadow."""
        if sys.platform != 'win32': return
        try:
            import ctypes
            hwnd = self._hwnd()
            if not hwnd: return
            # CL-0266: remove every piece of the Windows frame so only the launcher's own title bar buttons remain
            # (no system caption, min/max/close boxes, system menu or sizing border; resizing uses the page's edge handles).
            u = ctypes.windll.user32
            GWL_STYLE = -16
            WS_CAPTION, WS_THICKFRAME, WS_SYSMENU, WS_MINIMIZEBOX, WS_MAXIMIZEBOX = 0x00C00000, 0x00040000, 0x00080000, 0x00020000, 0x00010000
            get = getattr(u, 'GetWindowLongPtrW', u.GetWindowLongW); put = getattr(u, 'SetWindowLongPtrW', u.SetWindowLongW)
            get.restype = ctypes.c_ssize_t; put.restype = ctypes.c_ssize_t
            put.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_ssize_t]; get.argtypes = [ctypes.c_void_p, ctypes.c_int]
            st = get(hwnd, GWL_STYLE)
            put(hwnd, GWL_STYLE, st & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX))
            u.SetWindowPos(hwnd, 0, 0, 0, 0, 0, 0x0020 | 0x0002 | 0x0001 | 0x0004 | 0x0010)  # FRAMECHANGED | NOMOVE | NOSIZE | NOZORDER | NOACTIVATE
            dwm = ctypes.windll.dwmapi
            pref = ctypes.c_int(2)  # DWMWCP_ROUND
            dwm.DwmSetWindowAttribute(hwnd, 33, ctypes.byref(pref), 4)  # DWMWA_WINDOW_CORNER_PREFERENCE (Windows 11; ignored on 10)
            class MARGINS(ctypes.Structure):
                _fields_ = [('l', ctypes.c_int), ('r', ctypes.c_int), ('t', ctypes.c_int), ('b', ctypes.c_int)]
            dwm.DwmExtendFrameIntoClientArea(hwnd, ctypes.byref(MARGINS(1, 1, 1, 1)))  # gives a frameless window the system shadow
        except Exception:
            pass

    def close(self):
        if self.win: self.win.destroy()

    def native(self):
        return True


def main():
    if webview is None:
        import launcher  # Edge/Chrome app window, the old way
        return launcher.main()
    srv = server.make_server(int(os.environ.get('SVR06_LAUNCHER_PORT') or 0))
    url = 'http://127.0.0.1:%d/?native=1' % srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    api = Api()
    icon = os.path.join(server.UI, 'icon.ico')
    win = webview.create_window('SvR06 Launcher', url, js_api=api, width=1280, height=820, min_size=(960, 600),
                                frameless=True, easy_drag=False, background_color='#0b0e14', text_select=False)
    api.win = win
    try:
        win.events.shown += lambda: api.style_window()   # CL-0260 rounded corners + shadow
    except Exception:
        pass
    kw = {'gui': 'edgechromium'} if sys.platform == 'win32' else {}
    if os.path.isfile(icon): kw['icon'] = icon
    try:
        webview.start(private_mode=False, storage_path=os.path.join(server.ROOT, 'config', 'launcher_webview'), **kw)
    except TypeError:  # older pywebview without icon/storage_path
        webview.start(**({'gui': 'edgechromium'} if sys.platform == 'win32' else {}))
    os._exit(0)


if __name__ == '__main__':
    main()
