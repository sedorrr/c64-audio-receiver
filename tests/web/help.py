#!/usr/bin/env python3
"""
Browser test of the "Hilfe" tab (Playwright + Chromium): the manual comes from GET /hilfe gzip-compressed, exactly as
the firmware sends it (src/net/help_page.h). A real HTTP server, so the browser itself unpacks the gzip like it does
with the Pico (Playwright's route.fulfill would not). Checks: loaded only when the tab opens and only once, the
"? Hilfe zu diesem Tab" link of every tab jumps to its section, table of contents, links, phone width.
Usage: python3 tests/web/help.py   (after python3 tools/make_web_page.py)
"""
import json, os, re, sys, threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = open(os.path.join(ROOT, 'tools', 'web', 'index.html'), 'rb').read()
GZ = bytes(int(x) for x in re.findall(r'\d+', open(os.path.join(ROOT, 'src', 'net', 'help_page.h')).read().split('{', 1)[1].split('}')[0]))
STATUS = {"fw": "c64audio-fw", "build": "Oct 18 2026 10:00:00", "wifi": "Fritz!Box", "ssid": "Fritz!Box", "ip": "192.168.1.79", "sta": True,
          "name": "c64audio", "cfgname": "", "pwdefault": False, "u64": "192.168.1.168", "src": 0, "u64state": "Stream läuft", "play": True,
          "state": "spielt", "rx": True, "from": "192.168.1.168", "direct": True, "pps": 250, "vol": 80, "fill": 83.2, "avg": 84.0, "target": 80,
          "ppm": -17, "rate": 47982.2, "missing": 0, "late": 0, "dup": 0, "underruns": 0, "concealed": 0, "silent": 0, "skipped": 0, "restarts": 0,
          "prefills": 1, "qdrop": 0, "behind": 0, "rejected": 0, "bad": 0, "fwd_on": True, "fwd": "c64viz.local", "fwdip": "192.168.1.231",
          "fpps": 250, "fwderr": 0, "taken": 0, "contested": False, "np": "", "lat": 80, "fwdpw": True, "npr": "", "nprsent": 0,
          "scanning": False, "u64list": [{"label": "Elite II", "addr": "192.168.1.168", "pw": True}] + [{"label": "", "addr": "", "pw": False}] * 3}
HITS = []

class Pico(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype, extra=()):
        self.send_response(200); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body)))
        for k, v in extra: self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path == '/': self.send(PAGE, 'text/html; charset=utf-8')
        elif self.path == '/status': self.send(json.dumps(STATUS).encode(), 'application/json')
        elif self.path == '/hilfe': HITS.append(self.path); self.send(GZ, 'text/html; charset=utf-8', [('Content-Encoding', 'gzip')])
        elif self.path == '/fw/state': self.send(json.dumps({"state": "idle", "got": 0, "size": 0, "msg": "", "max": 2000000, "build": STATUS['build']}).encode(), 'application/json')
        else: self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); self.rfile.read(n); self.send(b'OK', 'text/plain')

fails = 0
def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok: fails += 1

srv = ThreadingHTTPServer(('127.0.0.1', 0), Pico); threading.Thread(target=srv.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:%d/' % srv.server_address[1]
with sync_playwright() as p:
    b = p.chromium.launch(); errors = []
    pg = b.new_page(viewport={"width": 420, "height": 900}); pg.on('pageerror', lambda e: errors.append(str(e)))
    pg.goto(url); pg.wait_for_timeout(800)
    check(not HITS, "manual not loaded before the tab is opened")
    pg.click("nav.tabs button[data-t=help]"); pg.wait_for_timeout(500)
    h2 = pg.evaluate("[...document.querySelectorAll('#helpbody h2')].map(e=>e.textContent)")
    check(len(HITS) == 1 and len(h2) > 8, f"tab loads /hilfe once (gzip unpacked by the browser), {len(h2)} chapters")
    check(pg.evaluate("document.querySelectorAll('.htoc li').length") == len(h2) - 1, "table of contents lists every chapter")
    pg.click("nav.tabs button[data-t=play]"); pg.click("nav.tabs button[data-t=help]"); pg.wait_for_timeout(300)
    check(len(HITS) == 1, "second opening uses the loaded manual")
    for t, hid in (('play', 'tab-wiedergabe'), ('set', 'tab-einstellungen'), ('sys', 'tab-system')):
        pg.click(f"nav.tabs button[data-t={t}]"); pg.click(f"section[data-tab={t}] .hl a"); pg.wait_for_timeout(300)
        top = pg.evaluate(f"document.getElementById('h-{hid}').getBoundingClientRect().top")
        sel = pg.evaluate("document.querySelector('nav.tabs button[aria-selected=true]').dataset.t")
        check(sel == 'help' and abs(top) < 5, f"tab {t}: context link opens the help at '{hid}' (top {top:.0f})")
    pg.click(".htoc a[data-h=fehlersuche]"); pg.wait_for_timeout(300)
    top = pg.evaluate("document.getElementById('h-fehlersuche').getBoundingClientRect().top")
    check(abs(top) < 5, "table of contents jumps to 'Fehlersuche'")
    pg.click("#helpbody a[data-h=passwort-vergessen]"); pg.wait_for_timeout(300)
    top = pg.evaluate("document.getElementById('h-passwort-vergessen').getBoundingClientRect().top")
    check(abs(top) < 5, "a link inside the text jumps to its section")
    bad = pg.evaluate("[...document.querySelectorAll('#helpbody a[data-h]')].filter(a=>!document.getElementById('h-'+a.dataset.h)).map(a=>a.dataset.h)")
    check(not bad, f"all links inside the manual have a target {bad}")
    check(pg.evaluate("document.documentElement.scrollWidth<=window.innerWidth"), "no horizontal page scroll at phone width (tables scroll in their box)")
    pg.evaluate("window.scrollTo(0,0)"); pg.screenshot(path=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tab_help.png'))
    # the Pico not answering /hilfe: a message instead of an empty tab, the next opening tries again
    HITS.clear(); pg2 = b.new_page(); pg2.on('pageerror', lambda e: errors.append(str(e)))
    pg2.route('**/hilfe', lambda r: r.abort()); pg2.goto(url); pg2.click("nav.tabs button[data-t=help]"); pg2.wait_for_timeout(400)
    check('nicht geladen' in pg2.inner_text('#helpbody'), "no connection: message in the help tab")
    pg2.unroute('**/hilfe'); pg2.click("nav.tabs button[data-t=play]"); pg2.click("nav.tabs button[data-t=help]"); pg2.wait_for_timeout(500)
    check(pg2.evaluate("document.querySelectorAll('#helpbody h2').length") > 8, "... and the next opening loads it")
    check(not errors, f"no JS errors {errors}")
    b.close()
print('all checks passed' if not fails else f'{fails} checks FAILED'); sys.exit(1 if fails else 0)
