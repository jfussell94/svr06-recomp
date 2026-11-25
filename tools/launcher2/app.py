#!/usr/bin/env python3
"""SvR06 Launcher 2 - native window.

A normal Windows window (WebView2 through pywebview): one set of minimize / maximize / close buttons, Aero snap, Win+arrows,
the system shadow and resize border all come from Windows, nothing is faked.  Without pywebview it opens in an Edge/Chrome app window.
Start with scripts\\LAUNCHER2.cmd or:  python tools\\launcher2\\app.py
"""
import json, os, subprocess, sys, threading, time, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import backend  # noqa: E402

try:
    import webview
except Exception:
    webview = None

TITLE = 'SvR06 Launcher'


def saved_window():
    w = backend.ui_load().get('window')
    if isinstance(w, dict) and w.get('w', 0) >= 900 and w.get('h', 0) >= 560: return w
    return None


def fallback_browser(url):
    for exe in (r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe', r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
                r'C:\Program Files\Google\Chrome\Application\chrome.exe'):
        if os.path.isfile(exe):
            subprocess.Popen([exe, '--app=' + url, '--window-size=1280,860'])
            return
    webbrowser.open(url)


def main():
    srv = backend.make_server(int(os.environ.get('SVR06_LAUNCHER_PORT') or 0))
    port = srv.server_address[1]
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = 'http://127.0.0.1:%d/' % port
    if webview is None:
        fallback_browser(url)
        try:
            while True: time.sleep(3600)
        except KeyboardInterrupt:
            return
    w = saved_window() or {'w': 1280, 'h': 860, 'x': None, 'y': None}
    win = webview.create_window(TITLE, url, width=int(w['w']), height=int(w['h']), x=w.get('x'), y=w.get('y'),
                                min_size=(900, 600), background_color='#0e1013', text_select=True, confirm_close=False)

    def on_closing():
        try:
            backend.ui_save({'window': {'w': win.width, 'h': win.height, 'x': win.x, 'y': win.y}})
        except Exception:
            pass
    try: win.events.closing += on_closing
    except Exception: pass
    kw = {'gui': 'edgechromium'} if sys.platform == 'win32' else {}
    icon = os.path.join(HERE, 'ui', 'icon.ico')
    if os.path.isfile(icon): kw['icon'] = icon
    store = os.path.join(backend.ROOT, 'config', 'launcher2_webview')
    try:
        webview.start(private_mode=False, storage_path=store, **kw)
    except TypeError:
        webview.start(**({'gui': 'edgechromium'} if sys.platform == 'win32' else {}))
    os._exit(0)


if __name__ == '__main__':
    main()
