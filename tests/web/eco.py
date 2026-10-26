#!/usr/bin/env python3
"""
Browser test of the card "Geräte im Netz" (tab Wiedergabe, Playwright + Chromium) against a mock of the Pico:
the way of the sound along the chain, every device with its state, warnings, "Neu suchen", loaded only while the tab
is open; and the status line for titles sent without the visualizer's password.
Usage: python3 tests/web/eco.py
"""
import json, os, re, sys, threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from playwright.sync_api import sync_playwright

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE = open(os.path.join(ROOT, 'tools', 'web', 'index.html'), 'rb').read()
GZ = bytes(int(x) for x in re.findall(r'\d+', open(os.path.join(ROOT, 'src', 'net', 'help_page.h')).read().split('{', 1)[1].split('}')[0]))
def dev(**k): return json.dumps(k)
DEVICES = {"list": [
    {"self": True, "age": 0, "j": dev(t="audio", n="c64audio", ip="192.168.1.79", b="Oct 18 2026 10:20:00", img="c64audio", up=300, st=1, rx=250,
                                      **{"from": "192.168.1.168"}, fw="c64viz.local", fwip="192.168.1.231", fp=250, src="192.168.1.168", srcl="Elite II")},
    {"age": 3, "j": dev(t="viz", n="c64viz", ip="192.168.1.231", b="Oct 18 2026 10:12:00", img="c64viz_wifi_ws2812_usb-350mA", st=1, sm=2, rx=250,
                        **{"from": "192.168.1.79"}, fw="c64viz2.local", fwip="192.168.1.224", fp=250, src="192.168.1.168", srcl="Elite II", wide="", wok=False)},
    {"age": 20, "j": dev(t="viz", n="c64viz2", ip="192.168.1.224", b="Oct 18 2026 10:12:00", img="c64viz_wifi_ws2812_usb-350mA", st=1, sm=2, rx=250,
                         **{"from": "192.168.1.231"}, fw="", fwip="", fp=0, src="", srcl="")},
    {"age": 1, "j": dev(t="audio", n="keller", ip="192.168.1.80", b="Oct 18 2026 10:20:00", img="c64audio_debug", st=0, rx=0,
                        **{"from": ""}, fw="kueche.local", fwip="", fp=0, src="192.168.1.168", srcl="Elite II")},
    {"age": 1, "j": "{kaputt"}]}
STATUS = {"fw": "c64audio-fw", "build": "Oct 18 2026 10:20:00", "wifi": "Fritz!Box", "ssid": "Fritz!Box", "ip": "192.168.1.79", "sta": True,
          "name": "c64audio", "cfgname": "", "pwdefault": False, "u64": "192.168.1.168", "src": 0, "u64state": "Stream läuft", "play": True,
          "state": "spielt", "rx": True, "from": "192.168.1.168", "direct": True, "pps": 250, "vol": 80, "fill": 83.2, "avg": 84.0, "target": 80,
          "ppm": -17, "rate": 47982.2, "missing": 0, "late": 0, "dup": 0, "underruns": 0, "concealed": 0, "silent": 0, "skipped": 0, "restarts": 0,
          "prefills": 1, "qdrop": 0, "behind": 0, "rejected": 0, "bad": 0, "fwd_on": True, "fwd": "c64viz.local", "fwdip": "192.168.1.231",
          "fpps": 250, "fwderr": 0, "taken": 0, "contested": False, "np": "Delta – Rob Hubbard", "lat": 80, "fwdpw": False,
          "npr": "gesendet (ohne Passwort)", "nprsent": 3, "scanning": False,
          "u64list": [{"label": "Elite II", "addr": "192.168.1.168", "pw": True}] + [{"label": "", "addr": "", "pw": False}] * 3}
ME = "c64audio"
def star(pi_on=True):
    """a Raspberry Pi jukebox as distributor: it feeds every Pico itself (one of its targets is off)"""
    pi = dict(t="pi", n="c64pi", ip="192.168.1.78", b="1.0", st=1 if pi_on else 0, sm=0, rx=251 if pi_on else 0,
              fw="c64audio,c64viz,c64viz2,c64viz3", fwip="192.168.1.79", fp=251, src="192.168.1.168", srcl="Elite II",
              title="Commando", **{"from": "192.168.1.168" if pi_on else ""})
    picos = [dict(t=t, n=n, ip=ip, st=1, sm=2, rx=251 if pi_on else 0, fw="", fwip="", fp=0, **{"from": "192.168.1.78" if pi_on else ""})
             for t, n, ip in (("audio", "c64audio", "192.168.1.79"), ("viz", "c64viz", "192.168.1.231"), ("viz", "c64viz2", "192.168.1.224"))]
    return [{"self": d["n"] == ME, "age": 2, "j": json.dumps(d)} for d in [pi] + picos]
HITS = {"devices": 0, "query": 0}

class Pico(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype, extra=()):
        self.send_response(200); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body)))
        for k, v in extra: self.send_header(k, v)
        self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        if self.path == '/': self.send(PAGE, 'text/html; charset=utf-8')
        elif self.path == '/status': self.send(json.dumps(STATUS).encode(), 'application/json')
        elif self.path == '/devices': HITS["devices"] += 1; self.send(json.dumps(DEVICES).encode(), 'application/json')
        elif self.path == '/hilfe': self.send(GZ, 'text/html; charset=utf-8', [('Content-Encoding', 'gzip')])
        elif self.path == '/fw/state': self.send(json.dumps({"state": "idle", "got": 0, "size": 0, "msg": "", "max": 2000000, "build": STATUS['build']}).encode(), 'application/json')
        else: self.send_response(404); self.send_header('Content-Length', '0'); self.end_headers()
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); self.rfile.read(n)
        if self.path == '/devices/query': HITS["query"] += 1
        self.send(b'OK', 'text/plain')

fails = 0
def check(ok, what):
    global fails
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok: fails += 1

srv = ThreadingHTTPServer(('127.0.0.1', 0), Pico); threading.Thread(target=srv.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:%d/' % srv.server_address[1]
with sync_playwright() as p:
    b = p.chromium.launch(); errors = []
    pg = b.new_page(viewport={"width": 420, "height": 1000}); pg.on('pageerror', lambda e: errors.append(str(e)))
    pg.goto(url); pg.wait_for_timeout(800)
    chain = pg.inner_text('#devchain'); lst = pg.inner_text('#devlist')
    print('   ' + chain.replace('\n', ' | ')); print('   ' + lst.replace('\n', ' | ')[:500])
    check(HITS["devices"] == 1, "the card is loaded once at the start")
    check("Elite II (192.168.1.168) → c64audio 250/s → c64viz 250/s → c64viz2 250/s" in chain, "the way of the sound, from the Ultimate along the chain")
    check(lst.count("Visualizer") == 2 and lst.count("Audio-Empfänger") == 2 and "kaputt" not in lst, "four devices, a broken entry is skipped")
    check("c64audio – Audio-Empfänger, 192.168.1.79 (dieses Gerät)" in lst and "Firmware Oct 18 2026 10:20" in lst, "this device first, with its firmware")
    check("Ton: Stream 250/s von Elite II (192.168.1.168) → leitet weiter an c64viz.local (250/s)" in lst, "receiver: stream from the Ultimate, forwarding")
    check("keller – Audio-Empfänger" in lst and "gestoppt" in lst and "Weiterleitung: kueche.local wird gesucht" in lst, "stopped receiver; forwarding target not found: warning")
    check("(seit 20 s still)" in lst, "a device not heard of for a while is marked")
    check(pg.get_attribute("#devlist a >> nth=1", "href") == "http://c64viz.local/", "names link to the device's page")
    pg.click("text=Neu suchen"); pg.wait_for_timeout(1200)
    check(HITS["query"] == 1 and HITS["devices"] >= 2, "\"Neu suchen\" asks all devices, then reloads the list")
    st = pg.inner_text('#st')
    check("Titel an den Visualizer: gesendet (ohne Passwort) (3 übergeben)" in st and pg.get_attribute("#st b.ok >> nth=-1", "class") == "ok",
          "status: titles sent without password, shown as fine")
    pg.screenshot(path=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tab_devices.png'), full_page=True)
    pg.click("nav.tabs button[data-t=set]"); n0 = HITS["devices"]; pg.wait_for_timeout(5500)
    check(HITS["devices"] == n0, "not loaded while another tab is open")
    pg.click("nav.tabs button[data-t=play]"); pg.wait_for_timeout(500)
    check(HITS["devices"] == n0 + 1, "loaded again when the tab opens")
    pg.click("section[data-tab=play] .card >> text=Was steht hier?"); pg.wait_for_timeout(600)
    top = pg.evaluate("document.getElementById('h-geraete-im-netz').getBoundingClientRect().top")
    check(abs(top) < 5, "\"Was steht hier?\" opens the manual at 'Geräte im Netz'")
    pg.click("nav.tabs button[data-t=play]"); pg.wait_for_timeout(300)
    # a Raspberry Pi as distributor: the way of the sound branches out, the Pi has its own name and shows the title
    DEVICES["list"] = star(); pg.evaluate("devLoad()"); pg.wait_for_timeout(300)
    chain = pg.inner_text("#devchain"); lst = pg.inner_text("#devlist")
    print("   " + chain.replace("\n", " | "))
    check("Elite II (192.168.1.168) → c64pi 251/s → [c64audio 251/s, c64viz 251/s, c64viz2 251/s, c64viz3 (bekommt nichts)]" in chain,
          "Pi feeding several Picos: a tree, a target that gets nothing is marked")
    check("c64pi – Raspberry Pi, 192.168.1.78" in lst and "♪ Commando" in lst and "pi," not in lst, "the Pi is named as such, with its title")
    DEVICES["list"] = star(False); pg.evaluate("devLoad()"); pg.wait_for_timeout(300)
    lst = pg.inner_text("#devlist")
    check("c64pi – Raspberry Pi" in lst and "leitet" not in lst.split("c64audio")[0], "Pi with the distributor off: no forwarding line")
    check(not errors, f"no JS errors {errors}")
    b.close()
print('all checks passed' if not fails else f'{fails} checks FAILED'); sys.exit(1 if fails else 0)
