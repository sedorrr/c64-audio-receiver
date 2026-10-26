#!/usr/bin/env python3
"""
Browser test of the receiver page opened by the setup assistant of a visualizer (http://<ip>/#setup=<json>):
settings tab filled in after the settings are loaded, changed fields marked, banner, link cleaned; saving sends
forwarding, the Ultimate as source and playing on (src, play) - what the assistant then sees in the beacon.
Usage: python3 tests/web/wizard.py
"""
import copy, json, os, sys, threading, urllib.parse
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = open(os.path.join(ROOT, 'tools', 'web', 'index.html'), 'rb').read()
BASE = {"fw": "c64audio-fw", "build": "Oct 18 2026 11:00:00", "wifi": "Fritz!Box", "ssid": "Fritz!Box", "ip": "192.168.1.79", "sta": True,
        "name": "c64audio", "cfgname": "", "pwdefault": False, "u64": "192.168.1.168", "src": 0, "u64state": "Wiedergabe aus", "play": False,
        "state": "aus", "rx": False, "from": "-", "direct": False, "pps": 0, "vol": 80, "fill": 0, "avg": 0, "target": 80,
        "ppm": 0, "rate": 47983.0, "missing": 0, "late": 0, "dup": 0, "underruns": 0, "concealed": 0, "silent": 0, "skipped": 0, "restarts": 0,
        "prefills": 0, "qdrop": 0, "behind": 0, "rejected": 0, "bad": 0, "fwd_on": False, "fwd": "", "fwdip": "-",
        "fpps": 0, "fwderr": 0, "taken": 0, "contested": False, "np": "", "lat": 80, "fwdpw": False, "npr": "", "nprsent": 0, "scanning": False,
        "u64list": [{"label": "Elite II", "addr": "192.168.1.168", "pw": True}] + [{"label": "", "addr": "", "pw": False}] * 3}
ST = {"s": copy.deepcopy(BASE)}
CONFIGS = []

class Pico(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(200); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == '/': self.send(PAGE, 'text/html; charset=utf-8')
        elif self.path == '/status': self.send(json.dumps(ST["s"]), 'application/json')
        elif self.path == '/devices': self.send('{"list":[]}', 'application/json')
        elif self.path == '/fw/state': self.send(json.dumps({"state": "idle", "got": 0, "size": 0, "msg": "", "max": 2000000, "build": BASE['build']}), 'application/json')
        else: self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); body = self.rfile.read(n).decode() if n else ''
        if self.path == '/config': CONFIGS.append(dict(urllib.parse.parse_qsl(body, keep_blank_values=True)))
        self.send('Gespeichert - der Pico startet neu und verbindet sich.' if self.path == '/config' else 'OK', 'text/plain')

fails = 0
def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok: fails += 1

srv = ThreadingHTTPServer(('127.0.0.1', 0), Pico); threading.Thread(target=srv.serve_forever, daemon=True).start()
URL = 'http://127.0.0.1:%d/' % srv.server_address[1]
def setup_url(w): return URL + '#setup=' + urllib.parse.quote(json.dumps(w))
def marked(pg): return pg.evaluate("[...document.querySelectorAll('.wizmark')].map(e=>e.id).sort().join(',')")

with sync_playwright() as p:
    b = p.chromium.launch(); errors = []
    def page():
        pg = b.new_page(viewport={"width": 420, "height": 1000}); pg.on('pageerror', lambda e: errors.append(str(e))); pg.on('dialog', lambda d: d.accept()); return pg
    # 1. first device of the chain: forwarding to c64viz, the Ultimate is already row 1 and the source
    pg = page(); pg.goto(setup_url({"v": 1, "by": "c64viz", "stream": 1, "fwd": "c64viz.local", "u64": "192.168.1.168", "u64l": "Elite II"})); pg.wait_for_timeout(1200)
    banner = pg.inner_text('#wizbanner')
    print('   ' + banner)
    check(pg.evaluate("document.querySelector('nav.tabs button[aria-selected=true]').dataset.t") == 'set' and pg.is_visible('#wizbanner'), "settings tab with the banner")
    check("Vom Einricht-Assistenten (auf c64viz) vorbereitet." in banner and "an c64viz.local weiter" in banner and "Wiedergabe eingeschaltet" in banner, "banner: who, forwarding, playing on")
    check("#setup" not in pg.url, "the link is cleaned (no second fill-in on reload)")
    check(pg.is_checked('#fwdon') and pg.input_value('#fwd') == 'c64viz.local' and marked(pg) == 'fwd,fwdon', "forwarding on to c64viz.local, both marked; the known Ultimate not")
    pg.screenshot(path=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tab_setup.png'))
    pg.click("section[data-tab=set] button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(400)
    c = CONFIGS[-1] if CONFIGS else {}
    check(c.get('fwd_on') == '1' and c.get('fwd') == 'c64viz.local' and c.get('src') == '0' and c.get('play') == '1' and c.get('a0') == '192.168.1.168',
          "saved: forwarding, source row 1, playing on")
    pg.close()
    # 2. an Ultimate not in the list: next free row, its name, becomes the source; last device: no forwarding
    ST["s"] = copy.deepcopy(BASE); ST["s"]["fwd_on"] = True; ST["s"]["fwd"] = "c64viz2.local"; ST["s"]["play"] = True
    pg = page(); pg.goto(setup_url({"v": 1, "by": "c64viz", "stream": 1, "fwd": "", "u64": "192.168.1.41", "u64l": "Elite"})); pg.wait_for_timeout(1200)
    check(pg.input_value('#a1') == '192.168.1.41' and pg.input_value('#l1') == 'Elite' and not pg.is_checked('#fwdon') and marked(pg) == 'a1,fwdon,l1',
          "new Ultimate in the next free row with its name, forwarding off, marked")
    check("keine Weiterleitung" in pg.inner_text('#wizbanner') and "eingeschaltet" not in pg.inner_text('#wizbanner'), "banner: last of the chain, already playing")
    pg.click("section[data-tab=set] button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(400)
    c = CONFIGS[-1]
    check(c.get('fwd_on') == '0' and c.get('src') == '1' and c.get('a1') == '192.168.1.41' and c.get('l1') == 'Elite' and c.get('play') == '1', "saved: source row 2, forwarding off")
    pg.close()
    # 3. all four rows taken: the last one is replaced, the old Ultimate's password is deleted with it
    ST["s"] = copy.deepcopy(BASE); ST["s"]["u64list"] = [{"label": "U%d" % i, "addr": "192.168.1.%d" % (10 + i), "pw": True} for i in range(4)]
    pg = page(); pg.goto(setup_url({"v": 1, "by": "c64viz", "stream": 1, "fwd": "c64viz.local", "u64": "192.168.1.168", "u64l": "Elite II"})); pg.wait_for_timeout(1200)
    check(pg.input_value('#a3') == '192.168.1.168' and pg.is_checked('#x3'), "all rows taken: the last one replaced, its old password marked for deleting")
    pg.click("section[data-tab=set] button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(400)
    check(CONFIGS[-1].get('src') == '3' and CONFIGS[-1].get('x3') == '1', "saved: source row 4, old password deleted")
    pg.close()
    # 4. normal visit: nothing filled in, no src/play sent
    ST["s"] = copy.deepcopy(BASE); n0 = len(CONFIGS)
    pg = page(); pg.goto(URL); pg.wait_for_timeout(1000)
    check(not pg.is_visible('#wizbanner') and marked(pg) == '', "without #setup: no banner, nothing marked")
    pg.click("nav.tabs button[data-t=set]"); pg.click("section[data-tab=set] button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(400)
    check(len(CONFIGS) == n0 + 1 and 'src' not in CONFIGS[-1] and 'play' not in CONFIGS[-1], "normal save: source and playing untouched")
    # 5. broken #setup: ignored
    pg2 = page(); pg2.goto(URL + '#setup=%7Bkaputt'); pg2.wait_for_timeout(1000)
    check(not pg2.is_visible('#wizbanner'), "broken #setup is ignored")
    check(not errors, f"no JS errors {errors}")
    b.close()
print('all checks passed' if not fails else f'{fails} checks FAILED'); sys.exit(1 if fails else 0)
