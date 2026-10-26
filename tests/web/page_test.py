#!/usr/bin/env python3
"""
Browser test of the web page (Playwright + Chromium): tools/web/index.html served by a small mock of the Pico.
Checks status display, sources, volume slider, the settings form (fields sent to /config) and the firmware check.
Usage: python3 tests/web/page_test.py
"""
import json, os, sys, threading, urllib.parse
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = open(os.path.join(ROOT, 'tools', 'web', 'index.html'), encoding='utf-8').read()
STATUS = {"fw": "c64audio-fw", "build": "Oct 14 2026 19:00:00", "wifi": "Fritz!Box", "ssid": "Fritz!Box", "ip": "192.168.1.50", "sta": True,
          "name": "c64audio", "cfgname": "", "pwdefault": False, "u64": "192.168.1.168", "src": 0, "u64state": "Stream läuft", "play": True,
          "state": "spielt", "rx": True, "from": "192.168.1.168", "direct": True, "pps": 250, "vol": 80, "fill": 83.2, "avg": 84.0, "target": 80,
          "ppm": -17, "rate": 47982.2, "missing": 3, "late": 0, "dup": 0, "underruns": 0, "concealed": 3, "silent": 0, "skipped": 0, "restarts": 0,
          "prefills": 1, "qdrop": 0, "behind": 0, "rejected": 0, "bad": 0, "fwd_on": True, "fwd": "c64viz.local", "fwdip": "192.168.1.231",
          "fpps": 250, "fwderr": 0, "taken": 0, "contested": False, "np": "Commando – Rob Hubbard", "lat": 80,
          "fwdpw": True, "npr": "übergeben", "nprsent": 7,
          "u64list": [{"label": "Elite II", "addr": "192.168.1.168", "pw": True}, {"label": "", "addr": "192.168.1.41", "pw": False},
                      {"label": "", "addr": "", "pw": False}, {"label": "", "addr": "", "pw": False}]}
LOG = []
SCAN = {"n": 0}
def scan_json():                     # the search in steps: Elite II first, then the C64 Ultimate and an Elite only on WLAN
    SCAN["n"] += 1; n = SCAN["n"]
    found = [{"ip": "192.168.1.168", "alt": "192.168.1.202", "link": "eth", "name": "Elite2", "product": "Ultimate 64-II"}]
    if n >= 3: found += [{"ip": "192.168.1.246", "alt": "192.168.1.103", "link": "eth", "name": "C64-Ultimate-Wohnzimmer", "product": "C64 Ultimate"},
                         {"ip": "192.168.1.138", "alt": "", "link": "wlan", "name": "Elite1", "product": "Ultimate 64 Elite"}]
    return {"running": n < 4, "done": min(254, n * 70), "total": 254, "found": found}

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype):
        b = body.encode() if isinstance(body, str) else body
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path == '/': self.send(200, PAGE, 'text/html; charset=utf-8')
        elif self.path == '/status': self.send(200, json.dumps(STATUS), 'application/json')
        elif self.path == '/u64/scan': self.send(200, json.dumps(scan_json()), 'application/json')
        elif self.path == '/fw/state': self.send(200, json.dumps({"state": "idle", "got": 0, "size": 0, "msg": "", "max": 2000000, "build": STATUS['build']}), 'application/json')
        else: self.send(404, 'Nicht gefunden', 'text/plain')
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); body = self.rfile.read(n).decode() if n else ''
        LOG.append((self.path, body))
        if self.path.startswith('/cmd'): self.send(200, 'OK', 'text/plain')
        elif self.path == '/u64/scan': SCAN["n"] = 0; self.send(200, json.dumps(scan_json()), 'application/json')
        elif self.path == '/config': self.send(200, 'Gespeichert - der Pico startet neu und verbindet sich.', 'text/plain')
        else: self.send(409, '{"ok":false}', 'application/json')

fails = 0
def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok: fails += 1

srv = ThreadingHTTPServer(('127.0.0.1', 0), H); threading.Thread(target=srv.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:%d/' % srv.server_address[1]
with sync_playwright() as p:
    b = p.chromium.launch(); pg = b.new_page(); errors = []
    pg.on('pageerror', lambda e: errors.append(str(e))); pg.on('dialog', lambda d: d.accept())
    pg.goto(url); pg.wait_for_function("document.getElementById('st').textContent.includes('Pakete/s')")
    st = pg.inner_text('#st')
    check('250 Pakete/s' in st and '192.168.1.168' in st and '(direkt)' in st, 'status: stream, source, direct')
    check('c64viz.local' in st and 'Commando' in st, 'status: forwarding and now playing')
    check('Titel an den Visualizer: übergeben (7 übergeben)' in st, 'status: title messages handed to the visualizer')
    check(pg.eval_on_selector_all('#src option', 'o => o.map(x => x.textContent)') == ['Elite II (192.168.1.168)', 'Ultimate 2 (192.168.1.41)'], 'sources: only the configured ones')
    check(pg.inner_text('#volv') == '80 %', 'volume shows the Pico value')
    check('47982,2' in pg.inner_text('#buf') and '-17' in pg.inner_text('#buf'), 'buffer + clock line')
    pg.select_option('#src', '1'); pg.wait_for_timeout(300)
    check(('/cmd?c=src&i=1', '') in LOG, 'choosing a source sends /cmd?c=src&i=1')
    pg.fill('#vol', '42'); pg.dispatch_event('#vol', 'change'); pg.wait_for_timeout(300)
    check(any(x[0] == '/cmd?c=vol&v=42' for x in LOG), 'volume slider sends /cmd?c=vol&v=42')
    pg.click("button:has-text('Stopp')"); pg.wait_for_timeout(200)
    check(('/cmd?c=stop', '') in LOG, 'stop button')
    # settings tab: filled from the status, sent as a form
    pg.click("nav button[data-t=set]")
    check(pg.input_value('#ssid') == 'Fritz!Box' and pg.input_value('#a0') == '192.168.1.168' and pg.input_value('#fwd') == 'c64viz.local'
          and pg.is_checked('#fwdon') and not pg.is_checked('#fed') and pg.input_value('#lat') == '80', 'settings form filled from the Pico')
    check(pg.get_attribute('#p0', 'placeholder') == '(gesetzt)' and pg.is_visible('#xl0') and not pg.is_visible('#xl1'), 'stored passwords: shown as set, can be deleted')
    pg.check('#fed')                                         # the Pi jukebox delivers: never ask the Ultimate
    pg.fill('#a2', 'ultimate.fritz.box'); pg.fill('#l2', 'Keller'); pg.fill('#lat', '120'); pg.fill('#devname', 'Wohnzimmer')
    pg.click("button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(400)
    cfg = [x for x in LOG if x[0] == '/config']
    f = dict(urllib.parse.parse_qsl(cfg[-1][1], keep_blank_values=True)) if cfg else {}
    check(f.get('a2') == 'ultimate.fritz.box' and f.get('l2') == 'Keller' and f.get('lat') == '120' and f.get('name') == 'wohnzimmer'
          and f.get('fwd_on') == '1' and f.get('fed') == '1' and f.get('p0') == '' and f.get('x0') == '0' and f.get('fwdpw') == '' and f.get('xfwdpw') == '0',
          'save sends all fields (name lower case, empty password = unchanged)')
    check(pg.get_attribute('#fwdpw', 'placeholder') == '(gesetzt)' and pg.is_visible('#xfwdl'), "visualizer's password: shown as set, can be deleted")
    # network search: progress, hits, "already entered", WLAN warning, taking one into the first free row
    pg.click('#scanbtn'); pg.wait_for_timeout(300)
    check(pg.is_disabled('#scanbtn') and 'suche' in pg.inner_text('#scanst'), 'search running: progress shown, button locked')
    pg.wait_for_function("document.getElementById('scanst').textContent.includes('gefunden')", timeout=8000)
    res = pg.inner_text('#scanres'); print('   search: ' + res.replace('\n', ' | '))
    check('3 gefunden' in pg.inner_text('#scanst') and not pg.is_disabled('#scanbtn'), 'search finished: 3 found')
    check('192.168.1.168 (Ethernet)' in res and 'auch unter 192.168.1.202' in res and res.count('(eingetragen)') == 1, 'Elite II: Ethernet address, other address, already entered')
    check(res.count('nur per WLAN gefunden') == 1 and '192.168.1.138 (WLAN)' in res, 'device found only over WLAN: warned (stream over Ethernet only)')
    pg.click("#scanres button >> nth=0"); pg.wait_for_timeout(200)
    check(pg.input_value('#a3') == '192.168.1.246' and pg.input_value('#l3') == 'C64-Ultimate-Woh', 'taken into the first free row (name cut to 16)')
    check('Übernommen als Ultimate 4' in pg.inner_text('#msg') and pg.inner_text('#scanres').count('(eingetragen)') == 2, 'message, now marked as entered')
    pg.click("#scanres button"); pg.wait_for_timeout(200)
    check('Alle vier Plätze sind belegt' in pg.inner_text('#msg'), 'no free row left: refused with a message')
    pg.click("button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(400)
    f = dict(urllib.parse.parse_qsl([x for x in LOG if x[0] == '/config'][-1][1], keep_blank_values=True))
    check(f.get('a3') == '192.168.1.246' and f.get('l3') == 'C64-Ultimate-Woh', 'taken device saved with /config')
    pg.fill('#lat', '20'); n = len(LOG); pg.click("button:has-text('Speichern und neu starten')"); pg.wait_for_timeout(200)
    check(len(LOG) == n and '40 bis 300' in pg.inner_text('#msg'), 'buffer 20 ms: refused in the page')
    # firmware check: a .uf2 without the marker is refused before anything is sent
    pg.click("nav button[data-t=sys]")
    blk = bytearray(512); import struct
    struct.pack_into('<8I', blk, 0, 0x0A324655, 0x9E5D5157, 0x2000, 0x10000000, 256, 0, 1, 0xe48bff59); struct.pack_into('<I', blk, 508, 0x0AB16F30)
    blk[32:32+11] = b'c64viz_wifi'
    path = os.path.join(ROOT, 'tests', 'web', 'viz.uf2'); open(path, 'wb').write(blk)
    pg.set_input_files('#fwfile', path); pg.wait_for_timeout(300); os.remove(path)
    check('keine Firmware des C64-Audioempf' in pg.inner_text('#fwmsg'), 'visualizer firmware refused by the page')
    check(not errors, 'no JavaScript errors' + (': ' + '; '.join(errors) if errors else ''))
    # contested + no source configured
    STATUS.update(contested=True, u64list=[{"label": "", "addr": "", "pw": False}] * 4, rx=False)
    pg.wait_for_timeout(1500); st = pg.inner_text('#st')
    check('immer wieder' in st and 'kein Ultimate eingetragen' in st and 'kommt nicht an' in st, 'warnings: contested stream, no Ultimate set up')
    b.close()
print('\n%d FAILED' % fails if fails else '\nall ok'); sys.exit(1 if fails else 0)
