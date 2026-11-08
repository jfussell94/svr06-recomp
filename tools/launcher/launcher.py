#!/usr/bin/env python3
"""SvR06 Launcher (web edition). Start with:  pythonw tools\\launcher\\launcher.py
Starts the local backend (server.py) and opens the UI in its own Edge/Chrome app window (no tabs, no address bar).
Closing that window ends the launcher."""
import os, subprocess, sys, threading, time, webbrowser
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server


def find_browser():
    pf = [os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), os.environ.get('ProgramFiles', r'C:\Program Files'), os.environ.get('LOCALAPPDATA', '')]
    rels = [r'Microsoft\Edge\Application\msedge.exe', r'Google\Chrome\Application\chrome.exe', r'BraveSoftware\Brave-Browser\Application\brave.exe', r'Chromium\Application\chrome.exe']
    for rel in rels:
        for base in pf:
            p = os.path.join(base, rel)
            if base and os.path.isfile(p): return p
    return None


def main():
    port = int(os.environ.get('SVR06_LAUNCHER_PORT') or 0)
    srv = server.make_server(port)
    port = srv.server_address[1]
    url = 'http://127.0.0.1:%d/' % port
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    if os.environ.get('SVR06_LAUNCHER_NOBROWSER'):
        print(url); sys.stdout.flush()
        while True: time.sleep(3600)
    exe = find_browser()
    proc = None
    if exe:
        prof = os.path.join(server.ROOT, 'config', 'launcher_profile')
        os.makedirs(prof, exist_ok=True)
        proc = subprocess.Popen([exe, '--app=' + url, '--user-data-dir=' + prof, '--window-size=1280,820', '--no-first-run', '--no-default-browser-check',
                                 '--disable-features=Translate', '--disable-session-crashed-bubble', '--hide-crash-restore-bubble'])
    else:
        webbrowser.open(url)
    # watchdog: the window pings every few seconds; when it stops (window closed) the launcher exits too
    t0 = time.time()
    while True:
        time.sleep(1.5)
        last = server.LAST_PING[0]
        if last and time.time() - last > 25: break
        if not last and time.time() - t0 > 90: break
        if proc is not None and proc.poll() is not None and time.time() - t0 > 8 and last and time.time() - last > 8: break
    os._exit(0)


if __name__ == '__main__':
    main()
