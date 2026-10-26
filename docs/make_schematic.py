#!/usr/bin/env python3
"""
Draws the minimal wiring of the audio receiver: docs/schematic.svg (English) and docs/schematic_de.svg (German),
plus PNG versions if Playwright + Chromium are installed.
    python3 docs/make_schematic.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 1180, 860
FONT = "DejaVu Sans, Arial, Helvetica, sans-serif"
BLUE, RED, BLACK, GREY, VIOLET, GREEN = "#1f5fbf", "#c0392b", "#1a1a1a", "#7f8c8d", "#7d3c98", "#1e8449"

T = {
    "en": {
        "title": "C64 audio receiver  /  Pico 2 W + PCM5102A  -  minimal wiring",
        "sub": "No other parts. Signal = blue, +5 V = red, GND = black, 3.3 V = violet. Dashed = optional.",
        "pico_sub": "USB power (5 V charger)", "module": "PCM5102A module", "module2": "(purple board, 32 x 17 mm, 6-pin header)",
        "jack": "3.5 mm jack", "cable": "cable 3.5 mm jack -> 2 x RCA", "stereo": "Stereo: AUX / TAPE / TUNER in", "stereo2": "(not PHONO)",
        "button": "button (optional)", "pads": "Solder pads (back of the module):",
        "padrows": ["H1L FLT  -> L", "H2L DEMP -> L", "H3L XSMT -> H  (else: muted!)", "H4L FMT  -> L  (I2S)"],
        "notes": [
            "1) VIN of the module to VSYS (pin 39 = USB 5 V): the module's own regulator makes clean 3.3 V for the DAC.",
            "2) SCK to GND (pin or bridge on the board): the PCM5102A then makes its clock from BCK - no MCLK wire needed.",
            "3) Jumpers open: use the pads 1-4 on the long edge instead (FLT, DEMP, FMT to GND, XSMT to the A3V3 pad).",
            "4) Full volume = 2.1 V RMS, more than most old line inputs expect: start at 50-80 % on the web page.",
            "5) Hum? Use a charger without earth pin, or power the Pico from the stereo's USB socket.",
            "6) GP20/21/22 do not collide with the C64 visualizer's pins - the receiver can later move into its Pico.",
        ],
    },
    "de": {
        "title": "C64-Audioempfänger  /  Pico 2 W + PCM5102A  -  Minimalaufbau",
        "sub": "Keine weiteren Bauteile. Signal = blau, +5 V = rot, GND = schwarz, 3,3 V = violett. Gestrichelt = optional.",
        "pico_sub": "Versorgung über USB (5-V-Ladegerät)", "module": "PCM5102A-Modul", "module2": "(lila Platine, 32 x 17 mm, 6-polige Leiste)",
        "jack": "3,5-mm-Buchse", "cable": "Kabel 3,5-mm-Klinke -> 2 x Cinch", "stereo": "Stereoanlage: AUX / TAPE / TUNER", "stereo2": "(nicht PHONO)",
        "button": "Taster (optional)", "pads": "Lötbrücken (Rückseite des Moduls):",
        "padrows": ["H1L FLT  -> L", "H2L DEMP -> L", "H3L XSMT -> H  (sonst stumm!)", "H4L FMT  -> L  (I2S)"],
        "notes": [
            "1) VIN des Moduls an VSYS (Pin 39 = USB 5 V): der Regler auf dem Modul macht saubere 3,3 V für den DAC.",
            "2) SCK an GND (Pin oder Brücke auf der Platine): der PCM5102A erzeugt seinen Takt dann aus BCK - kein MCLK-Kabel nötig.",
            "3) Lötbrücken offen: stattdessen die Pads 1-4 an der Längsseite (FLT, DEMP, FMT an GND, XSMT an das Pad A3V3).",
            "4) Volle Lautstärke = 2,1 V eff., mehr als alte Line-Eingänge meist erwarten: auf der Webseite mit 50-80 % beginnen.",
            "5) Brummen? Ladegerät ohne Schutzkontakt verwenden oder den Pico aus der USB-Buchse der Anlage versorgen.",
            "6) GP20/21/22 stören die Pins des C64-Visualizers nicht - der Empfänger kann später in dessen Pico wandern.",
        ],
    },
}


class Svg:
    def __init__(self):
        self.o = []

    def add(self, s):
        self.o.append(s)

    def rect(self, x, y, w, h, fill="#fff", stroke=BLACK, sw=2, rx=6, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')

    def text(self, x, y, s, size=15, color=BLACK, anchor="start", weight="normal", mono=False):
        f = "DejaVu Sans Mono, Menlo, monospace" if mono else FONT
        s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        self.add(f'<text x="{x}" y="{y}" font-family="{f}" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}">{s}</text>')

    def line(self, pts, color=BLUE, sw=3, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        p = " ".join(f"{x},{y}" for x, y in pts)
        self.add(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{sw}" stroke-linejoin="round"{d}/>')

    def dot(self, x, y, color=BLACK, r=5):
        self.add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')

    def svg(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
                f'<rect width="{W}" height="{H}" fill="#ffffff"/>' + "".join(self.o) + "</svg>")


def draw(lang):
    t = T[lang]
    s = Svg()
    s.text(30, 40, t["title"], 24, weight="bold")
    s.text(30, 66, t["sub"], 15, GREY)

    # ---- Pico 2 W (pins on its right side: physical pin numbers)
    px, py, pw, ph = 40, 110, 300, 470
    s.rect(px, py, pw, ph, fill="#e9f7ef", stroke=GREEN)
    s.text(px + 20, py + 34, "Raspberry Pi Pico 2 W", 19, weight="bold")
    s.text(px + 20, py + 58, t["pico_sub"], 14, GREY)
    pins = [  # (y, label, phys, colour)
        (py + 110, "VSYS", "39", RED), (py + 150, "GND", "38", BLACK), (py + 230, "GP20", "26", BLUE),
        (py + 270, "GP21", "27", BLUE), (py + 350, "GP22", "29", BLUE),
        (py + 430, "GP15", "20", BLUE)]
    for y, lab, ph_, col in pins:
        s.text(px + pw - 14, y + 5, f"{lab}  ({ph_})", 15, col, "end", "bold", mono=True)
        s.dot(px + pw, y, col)

    # ---- PCM5102A module
    mx, my, mw, mh = 600, 110, 270, 470
    s.rect(mx, my, mw, mh, fill="#f4ecf7", stroke=VIOLET)
    s.text(mx + 18, my + 34, t["module"], 19, weight="bold")
    s.text(mx + 18, my + 56, t["module2"], 13, GREY)
    mpins = {"VIN": my + 110, "GND": my + 150, "SCK": my + 190, "DIN": my + 230, "BCK": my + 270, "LCK": my + 350,
             "FLT": my + 390, "DMP": my + 412, "FMT": my + 434, "XMT": my + 456}
    for lab, y in mpins.items():
        s.text(mx + 16, y + 5, lab, 15, BLACK, weight="bold", mono=True)
        s.dot(mx, y, BLACK if lab in ("GND", "SCK") else (RED if lab == "VIN" else BLUE))
    s.text(mx + 70, my + 400, "-> " + ("siehe Lötbrücken" if lang == "de" else "see solder pads"), 13, GREY)

    # wires Pico -> module
    def wire(y1, y2, col, xm):
        s.line([(px + pw, y1), (xm, y1), (xm, y2), (mx, y2)], col)
    wire(py + 110, mpins["VIN"], RED, 470)
    wire(py + 150, mpins["GND"], BLACK, 450)
    s.line([(450, mpins["GND"]), (450, mpins["SCK"]), (mx, mpins["SCK"])], BLACK)
    s.dot(450, mpins["GND"])
    wire(py + 230, mpins["DIN"], BLUE, 430)
    wire(py + 270, mpins["BCK"], BLUE, 410)
    wire(py + 350, mpins["LCK"], BLUE, 410)
    s.text(470, py + 100, "+5 V", 14, RED, weight="bold")
    s.text(380, mpins["DIN"] - 8, "DIN", 13, BLUE)
    s.text(380, mpins["BCK"] - 8, "BCK", 13, BLUE)
    s.text(380, mpins["LCK"] - 8, "LRCK", 13, BLUE)
    # optional button GP15 -> GND
    by = py + 430
    s.line([(px + pw, by), (420, by)], BLUE, dash="7 5")
    s.rect(420, by - 18, 90, 36, fill="#fff", stroke=GREY, dash="7 5")
    s.text(465, by + 5, "⏚ GND", 13, BLACK, "middle")
    s.text(420, by + 36, t["button"], 13, GREY)

    # ---- jack, cable, stereo
    jx, jy = mx + mw, my + 250
    s.rect(jx - 16, jy - 30, 32, 60, fill="#222", stroke=BLACK, rx=4)
    s.text(jx - 70, jy + 54, t["jack"], 13, GREY)
    s.line([(jx + 16, jy), (960, jy)], BLACK, sw=6)
    s.text(1055, jy + 104, t["cable"], 13, GREY, "middle")
    sx, sy = 960, jy - 90
    s.rect(sx, sy, 190, 180, fill="#fdf2e9", stroke="#ca6f1e")
    s.text(sx + 95, sy + 30, t["stereo"].split(":")[0], 17, anchor="middle", weight="bold")
    s.text(sx + 95, sy + 52, t["stereo"].split(":")[1].strip(), 13, anchor="middle")
    s.text(sx + 95, sy + 70, t["stereo2"], 12, GREY, "middle")
    for i, (lab, col) in enumerate((("L", "#eee"), ("R", "#e74c3c"))):
        cy = sy + 105 + i * 45
        s.add(f'<circle cx="{sx + 40}" cy="{cy}" r="14" fill="{col}" stroke="{BLACK}" stroke-width="2"/>')
        s.add(f'<circle cx="{sx + 40}" cy="{cy}" r="4" fill="{BLACK}"/>')
        s.text(sx + 66, cy + 5, lab, 15, weight="bold")
    s.line([(960, jy), (sx + 26, sy + 105)], BLACK, sw=3)
    s.line([(960, jy), (sx + 26, sy + 150)], BLACK, sw=3)

    # ---- solder pads
    s.text(900, 512, t["pads"], 14, weight="bold")
    for i, r in enumerate(t["padrows"]):
        s.text(905, 536 + 20 * i, r, 14, VIOLET, mono=True)

    # ---- notes
    ny = 640
    for i, n in enumerate(t["notes"]):
        s.text(40, ny + 26 * i, n, 14)
    return s.svg()


def main():
    out = []
    for lang, name in (("en", "schematic"), ("de", "schematic_de")):
        p = os.path.join(HERE, name + ".svg")
        open(p, "w", encoding="utf-8").write(draw(lang))
        out.append(p)
        print("written", p)
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("(no Playwright: PNGs not made)")
        return
    with sync_playwright() as pw:
        b = pw.chromium.launch()
        pg = b.new_page(viewport={"width": W, "height": H})
        for p in out:
            pg.goto("file://" + p)
            pg.screenshot(path=p[:-4] + ".png")
            print("written", p[:-4] + ".png")
        b.close()


if __name__ == "__main__":
    main()
