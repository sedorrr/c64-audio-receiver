#!/usr/bin/env python3
"""
Browser test of the backup on the web page (Playwright + Chromium): download = /backup as c64audio-<name>-<date>.json,
restore = /restore after asking; files of the visualizer or anything else are refused before anything is sent.
Usage: python3 tests/web/backup_test.py
"""
import json, os, sys, tempfile, threading, urllib.parse
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = open(os.path.join(ROOT, 'tools', 'web', 'index.html'), encoding='utf-8').read()
STATUS = {"fw": "c64audio-fw", "build": "Oct 15 2026 22:00:00", "wifi": "MeinWLAN", "ssid": "MeinWLAN", "ip": "192.168.1.79", "sta": True,
          "name": "wohnzimmer", "cfgname": "wohnzimmer", "pwdefault": False, "u64": "192.168.1.168", "src": 0, "u64state": "Stream läuft", "play": True,
          "state": "spielt", "rx": True, "from": "192.168.1.168", "direct": True, "pps": 250, "vol": 80, "fill": 80.0, "avg": 80.0, "target": 80,
          "ppm": 0, "rate": 47982.2, "missing": 0, "late": 0, "dup": 0, "underruns": 0, "concealed": 0, "silent": 0, "skipped": 0, "restarts": 0,
          "prefills": 1, "qdrop": 0, "behind": 0, "rejected": 0, "bad": 0, "fwd_on": True, "fwd": "c64viz.local", "fwdip": "192.168.1.231",
          "fpps": 250, "fwderr": 0, "taken": 0, "contested": False, "np": "", "lat": 80, "fwdpw": True, "npr": "", "nprsent": 0, "scanning": False,
          "u64list": [{"label": "Elite II", "addr": "192.168.1.168", "pw": False}] + [{"label": "", "addr": "", "pw": False}] * 3}
BACKUP = {"fmt": "c64audio-backup", "ver": 1, "build": STATUS["build"], "name": "wohnzimmer", "netcfg": "02c0d1a0" + "00" * 700, "prefs": "01f0d1a0" + "00" * 12}
POSTS = []

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype='text/plain; charset=utf-8'):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == '/': self.send(200, PAGE, 'text/html; charset=utf-8')
        elif self.path == '/status': self.send(200, json.dumps(STATUS), 'application/json')
        elif self.path == '/backup': self.send(200, json.dumps(BACKUP), 'application/json')
        elif self.path == '/fw/state': self.send(200, json.dumps({"state": "idle", "got": 0, "size": 0, "msg": "", "max": 2000000, "build": STATUS['build']}), 'application/json')
        else: self.send(404, 'Nicht gefunden')
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); body = self.rfile.read(n).decode() if n else ''
        POSTS.append((self.path, body))
        if self.path == '/restore':
            q = dict(urllib.parse.parse_qsl(body))
            if q.get('netcfg', '').startswith('bad'): self.send(400, 'Die Sicherung passt nicht (WLAN-/Ultimate-Einstellungen fehlen oder sind beschädigt).')
            else: self.send(200, 'Einstellungen wiederhergestellt - der Pico startet gleich neu.')
        else: self.send(200, 'OK')

fails = 0
def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok: fails += 1

srv = ThreadingHTTPServer(('127.0.0.1', 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:%d/' % srv.server_address[1]
tmp = tempfile.mkdtemp()
with sync_playwright() as p:
    b = p.chromium.launch(); ctx = b.new_context(accept_downloads=True); pg = ctx.new_page(); errors = []; dialogs = []
    pg.on('pageerror', lambda e: errors.append(str(e)))
    def on_dialog(d): dialogs.append(d.message); d.accept()
    pg.on('dialog', on_dialog)
    pg.goto(url); pg.wait_for_function("document.getElementById('st').textContent.includes('Pakete/s')")
    pg.click("nav button[data-t=sys]")
    with pg.expect_download() as dl: pg.click("text=Sicherung herunterladen")
    d = dl.value; path = os.path.join(tmp, d.suggested_filename); d.save_as(path); data = json.load(open(path))
    check(d.suggested_filename.startswith('c64audio-wohnzimmer-') and d.suggested_filename.endswith('.json'), 'file name c64audio-<name>-<date>.json: ' + d.suggested_filename)
    check(data['netcfg'] == BACKUP['netcfg'] and data['prefs'] == BACKUP['prefs'] and data['fmt'] == 'c64audio-backup' and data.get('date'), 'file has the blocks and the date')
    check('Gespeichert: c64audio-wohnzimmer-' in pg.inner_text('#bkmsg'), 'message: saved')
    # restore it
    pg.set_input_files('#bkfile', path); pg.wait_for_timeout(600)
    check(dialogs and 'wohnzimmer' in dialogs[-1] and 'startet danach neu' in dialogs[-1], 'asks first (name, restart)')
    rs = [x for x in POSTS if x[0] == '/restore']
    q = dict(urllib.parse.parse_qsl(rs[-1][1])) if rs else {}
    check(len(rs) == 1 and q.get('netcfg') == BACKUP['netcfg'] and q.get('prefs') == BACKUP['prefs'] and q.get('reboot') == '1', 'restore sends netcfg, prefs, reboot=1')
    check('startet neu' in pg.inner_text('#bkmsg'), 'message: restarts')
    # the Pico refuses a damaged one: its answer is shown
    bad = dict(data, netcfg='bad'); p2 = os.path.join(tmp, 'bad.json'); json.dump(bad, open(p2, 'w'))
    pg.set_input_files('#bkfile', p2); pg.wait_for_timeout(600)
    check('passt nicht' in pg.inner_text('#bkmsg'), "the Pico's refusal is shown")
    # foreign files: nothing is sent
    n = len(POSTS); nd = len(dialogs)
    viz = {"fmt": "c64viz-backup", "ver": 1, "name": "c64viz", "netcfg": "05", "jbprefs": "", "settings": ""}
    p3 = os.path.join(tmp, 'viz.json'); json.dump(viz, open(p3, 'w'))
    pg.set_input_files('#bkfile', p3); pg.wait_for_timeout(400)
    check('Sicherung des C64-Visualizers' in pg.inner_text('#bkmsg') and len(POSTS) == n and len(dialogs) == nd, 'visualizer backup refused by the page (nothing sent)')
    p4 = os.path.join(tmp, 'x.json'); open(p4, 'w').write('kein json')
    pg.set_input_files('#bkfile', p4); pg.wait_for_timeout(400)
    check('keine Sicherung des C64-Audio' in pg.inner_text('#bkmsg') and len(POSTS) == n, 'other file refused')
    check(not errors, 'no JavaScript errors' + (': ' + '; '.join(errors) if errors else ''))
    b.close()
print('\n%d FAILED' % fails if fails else '\nall ok'); sys.exit(1 if fails else 0)
