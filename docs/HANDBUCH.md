# C64-Audioempfänger – Handbuch

Der Audioempfänger holt den **Audio-Stream eines Ultimate 64** (z. B. Elite II) per WLAN und gibt ihn über ein
I2S-DAC-Modul **PCM5102A** an den Cinch-Eingängen einer Stereoanlage aus – echtes Stereo mit 48 kHz, ohne Kabel vom
C64 zur Anlage. Gleichzeitig reicht er den Stream an den **C64-Visualizer** weiter, denn ein Ultimate streamt immer
nur an ein Ziel.

Er läuft auf einem **Raspberry Pi Pico 2 W** (er braucht WLAN). Bedient wird er über seine Weboberfläche
(**http://c64audio.local**) und auf Wunsch über einen Taster.

Dieses Handbuch steht auch in der Weboberfläche im Tab **Hilfe**. Wie man die Firmware selbst baut, beschreibt die
README des Projekts.

## Was der Empfänger macht

- Er bittet den gewählten Ultimate um seinen Audio-Stream und spielt ihn ab, sobald genug gepuffert ist
  (Standard 80 ms).
- Ein **Jitter-Puffer** sortiert die Pakete nach ihrer Sequenznummer: Pakete in falscher Reihenfolge oder doppelt schaden nicht.
- **Kein Resampling:** Der Ultimate läuft mit rund 47 983 Hz statt 48 000 Hz. Der Empfänger stellt seinen eigenen
  Ausgabetakt so nach, dass er dem Ultimate genau folgt (nach etwa einer Minute eingeregelt).
- **Aussetzer werden verdeckt statt zu knacken:** Fehlt ein Paket, spielt er das vorige zeitlich gespiegelt und leiser
  werdend noch einmal (bis 12 ms), danach Stille mit sanftem Aus- und Einblenden.
- **Weiterleitung:** Jedes empfangene Paket geht unverändert an den Visualizer (abschaltbar), ebenso die Titelmeldungen
  der PC-Jukebox.
- Bis zu **vier Ultimates** lassen sich eintragen; einer davon ist die Quelle. Eine Netzwerksuche findet sie.
- Lautstärke, Quelle und Start/Stopp bleiben nach dem Ausschalten erhalten.
- Er meldet sich alle 10 s im Heimnetz und zeigt alle Visualizer und Audioempfänger mit dem Weg des Tons
  (siehe [Geräte im Netz](#geraete-im-netz)).

## Aufbau

### Anschluss des PCM5102A

Sechs Kabel vom Pico zum PCM5102A-Modul:

| Pico 2 W | PCM5102A-Modul |
|---|---|
| VSYS (Pin 39) | VIN (5 V, das Modul hat einen eigenen 3,3-V-Regler) |
| GND (Pin 38) | GND **und SCK** |
| GP20 (Pin 26) | DIN |
| GP21 (Pin 27) | BCK |
| GP22 (Pin 29) | LCK |
| GP15 (Pin 20) – optional | Taster gegen GND |

**SCK an GND** (Pin oder die Brücke neben SCK auf der Vorderseite): Der PCM5102A erzeugt seinen Takt dann selbst aus BCK.

Andere Bauformen des Moduls werden genauso angeschlossen. Hat ein Modul keinen eigenen Regler, kommt VIN an
**3V3 OUT (Pin 36)** statt an VSYS. Den Schaltplan zeigt die README (`docs/schematic_de.png`).

### Lötbrücken am Modul

Das Modul hat auf der Rückseite vier Lötbrücken, die oft **offen** geliefert werden – dann bleibt es stumm:

| Brücke | Funktion | Stellung |
|---|---|---|
| H1L | FLT (Filter) | L |
| H2L | DEMP (De-Emphasis) | L |
| H3L | XSMT (Stummschaltung) | **H** (sonst stumm!) |
| H4L | FMT (Format) | L = I2S |

Statt der Brücken gehen auch die Pads 1–4 an der Längsseite (FLT, DEMP, XSMT, FMT): FLT, DEMP und FMT an GND,
XSMT an das Pad A3V3.

### An die Stereoanlage

- Kabel 3,5-mm-Klinke → 2 × Cinch an einen Line-Eingang: **AUX, TAPE, TUNER oder CD** – nicht PHONO.
- Bei 100 % Lautstärke liefert der PCM5102A 2,1 V eff., mehr als alte Line-Eingänge meist erwarten. Mit 50–80 %
  beginnen; Standard ist 80 % (etwa −4 dB).
- Brummt es, ein Ladegerät ohne Schutzkontakt verwenden oder den Pico aus der USB-Buchse der Anlage versorgen.

### Taster und LED

Der Taster ist optional (GP15 gegen GND):

| Taster | Wirkung |
|---|---|
| kurz drücken | Start / Stopp |
| 1 s halten | nächster eingetragener Ultimate als Quelle |
| beim Einstecken halten | USB-Laufwerksmodus (zum Aufspielen einer Firmware) |

Die LED auf dem Pico:

| LED | Bedeutung |
|---|---|
| blinkt schnell | verbindet mit dem WLAN |
| Doppelblinken | eigener Zugangspunkt (siehe [Erste Einrichtung](#erste-einrichtung)) |
| kurzes Aufblitzen alle 2 s | bereit, spielt aber nicht |
| leuchtet | spielt |

### Gehäuse

Für den 3D-Druck gibt es ein Gehäuse (`case/receiver_case.scad`, 64 × 57 × 35 mm): Pico und Modul liegen
nebeneinander, beide Stiftleisten nach unten, USB-Buchse und Klinke zeigen nach hinten. Zusammenbau: erst die sechs
Kabel aufstecken, dann Pico und Modul einlegen, Deckel aufdrücken. Einzelheiten zum Druck stehen in der README.

## Firmware aufspielen

| Datei | Wofür |
|---|---|
| `c64audio.uf2` | normale Firmware |
| `c64audio_debug.uf2` | zusätzlich das [Diagnose-Protokoll](#diagnose-protokoll) live über USB-Seriell |
| `wlan_reset.uf2` | löscht WLAN-, Ultimate- und Passwort-Einstellungen (siehe [Passwort vergessen](#passwort-vergessen)) |

Zum ersten Aufspielen BOOTSEL (oder den Taster an GP15) beim Einstecken halten und die `.uf2` auf das Laufwerk
kopieren, das der Pico dann zeigt. Danach geht es auch ohne Kabel über die Webseite
(siehe [Firmware-Update über die Webseite](#firmware-update-ueber-die-webseite)).

Lief auf dem Pico vorher die Firmware des Visualizers, übernimmt der Empfänger deren WLAN, Webseiten-Passwort und Ultimate.

## Erste Einrichtung

1. Pico einstecken. Ohne Einstellungen öffnet er seinen eigenen Zugangspunkt **C64-Audio** (Passwort `commodore64`).
   Mit dem Handy oder Laptop damit verbinden und **http://192.168.4.1** öffnen. Dort fragt die Seite nicht nach einem
   Passwort.
2. Im Tab **Einstellungen** eintragen:
   - WLAN-Name (2,4 GHz) und WLAN-Passwort des Heimnetzes,
   - den Ultimate mit Adresse (IP oder Name) und ggf. seinem Netzwerk-Passwort – oder
     **„Netzwerk nach Ultimates durchsuchen“** (geht erst im Heim-WLAN, siehe [Mehrere Ultimates und Netzwerksuche](#mehrere-ultimates-und-netzwerksuche)),
   - für den Visualizer die **Weiterleitung** (siehe [Weiterleitung an den Visualizer](#weiterleitung-an-den-visualizer)),
   - ein eigenes Passwort für die Webseite.
3. **„Speichern und neu starten“** – der Pico startet neu und meldet sich im Heim-WLAN an.
4. Im Heimnetz ist die Seite unter **http://c64audio.local** erreichbar (bzw. unter der IP, die der Router vergibt).
   Der Browser fragt nach dem Passwort; der Benutzername ist beliebig, das Passwort ab Werk `commodore64`.
5. Tab **Wiedergabe → Start**.

Bekommt ein Empfänger einen eigenen **Gerätenamen** (z. B. `wohnzimmer`), heißt er im Netz `wohnzimmer.local` und sein
Zugangspunkt `C64-Audio-wohnzimmer`. Erlaubt sind 1–16 Zeichen a–z, 0–9 und Bindestrich (nicht am Anfang oder Ende).
Ist der Name im Netz schon vergeben, hängt der Pico `-2` … `-9` an; der Statuskasten zeigt das an.

## Die Weboberfläche

Oben steht der Statuskasten, darunter die Tabs **Wiedergabe**, **Einstellungen**, **System** und **Hilfe**. Der Link
„? Hilfe zu diesem Tab“ oben in jedem Tab springt zum passenden Abschnitt dieses Handbuchs.

### Der Statuskasten

| Zeile | Bedeutung |
|---|---|
| WLAN | Netzname, IP-Adresse und Gerätename (`c64audio.local`); im eigenen Zugangspunkt „eigener Zugangspunkt C64-Audio“ |
| Wiedergabe | „spielt“, „füllt den Puffer“ oder „gestoppt“, dazu die gewählte Quelle |
| Ultimate | was der Pico gerade mit dem Ultimate macht (siehe unten) |
| Stream | Pakete pro Sekunde (normal 250) und von welcher Adresse; „direkt“ = vom Ultimate, „weitergeleitet“ = von einem anderen Gerät |
| Weiterleitung | Ziel, seine Adresse und weitergeleitete Pakete pro Sekunde (nur wenn eingeschaltet) |
| Läuft | Titel, den die PC-Jukebox gemeldet hat |
| Titel an den Visualizer | ob die Titelmeldungen beim Visualizer ankommen |

Die Zeile **Ultimate** zeigt zum Beispiel:

| Anzeige | Bedeutung |
|---|---|
| Stream läuft | der Ultimate streamt direkt an den Empfänger |
| Stream kommt weitergeleitet | der Ton kommt von einem anderen Gerät (z. B. PC-Jukebox) |
| warte auf den Stream / Stream angefordert | der Pico hat den Ultimate gefragt, es kommt noch nichts |
| Wiedergabe aus | gestoppt |
| nicht eingerichtet | kein Ultimate eingetragen |
| Adresse nicht gefunden | der eingetragene Name lässt sich nicht auflösen |
| nicht erreichbar / keine Antwort | der Ultimate ist aus oder die Adresse stimmt nicht |
| Passwort falsch (403) | das Netzwerk-Passwort des Ultimate stimmt nicht |

Gelb erscheint ein Hinweis, solange die Webseite noch das Standardpasswort benutzt. Eine rote Warnung erscheint, wenn
ein anderes Gerät den Stream immer wieder wegnimmt (siehe [Weiterleitung an den Visualizer](#weiterleitung-an-den-visualizer)).

### Tab „Wiedergabe“

- **Start / Stopp:** Start bittet den Ultimate um den Stream; der Ton kommt, sobald der Puffer gefüllt ist.
  Stopp blendet aus und beendet den Stream am Ultimate – aber nur, wenn er wirklich direkt an den Empfänger geht.
- **Quelle:** welcher der eingetragenen Ultimates spielt. Beim Wechsel hört der alte auf, der neue wird sofort gefragt.
- **Lautstärke:** 0–100 %, wirkt sofort. Gespeichert wird 2 s nach der letzten Änderung, ohne hörbare Lücke.
- **Puffer und Takt:** Füllstand, Ausgabetakt und die Zähler – erklärt unter [Puffer, Takt und Aussetzer](#puffer-takt-und-aussetzer).
  „Zähler zurücksetzen“ setzt sie auf null.
- **Geräte im Netz:** alle Visualizer und Audioempfänger im Heimnetz mit dem Weg des Tons – siehe
  [Geräte im Netz](#geraete-im-netz).

### Tab „Einstellungen“

| Feld | Bedeutung |
|---|---|
| Gerätename | Name im Netz (`<name>.local`), leer = `c64audio` |
| WLAN-Name, WLAN-Passwort | Heimnetz, nur 2,4 GHz; leeres Passwortfeld = unverändert |
| Ultimate-Geräte | bis zu vier: Name, Adresse (IP oder Name), Netzwerk-Passwort; „löschen“ entfernt ein gespeichertes Passwort |
| Netzwerk nach Ultimates durchsuchen | siehe [Mehrere Ultimates und Netzwerksuche](#mehrere-ultimates-und-netzwerksuche) |
| Stream kommt weitergeleitet | nie selbst beim Ultimate anfordern – siehe [Weitergeleiteter Stream](#weitergeleiteter-stream) |
| Weiterleitung an den Visualizer | Schalter, Ziel (IP oder Name) und Passwort der Visualizer-Webseite |
| Puffer | 40–300 ms, Standard 80 |
| Neues Passwort für diese Webseite | 6–32 Zeichen, leer = unverändert |

„Speichern und neu starten“ schreibt alles in den Flash; danach startet der Pico neu und verbindet sich neu.
Leer gelassene Passwortfelder behalten das gespeicherte Passwort.

### Tab „System“

- **Firmware-Update** – siehe [Firmware-Update über die Webseite](#firmware-update-ueber-die-webseite).
- **Sicherung** – siehe [Sicherung](#sicherung).
- **Diagnose** – zeigt das [Diagnose-Protokoll](#diagnose-protokoll).

### Tab „Hilfe“

Dieses Handbuch mit Inhaltsverzeichnis. Der Pico hält es komprimiert im Flash und lädt es erst, wenn der Tab
geöffnet wird.

## Der Stream vom Ultimate

- Bei **Start** schickt der Pico dem Ultimate `PUT /v1/streams/audio:start?ip=<Pico>:11001`; der Ultimate schickt
  dann 250 UDP-Pakete pro Sekunde an Port 11001 des Pico. Bei **Stopp** schickt er `audio:stop`.
- Ist am Ultimate ein Netzwerk-Passwort gesetzt, muss es beim Ultimate eingetragen sein.
- Die Ultimates senden den Stream **nur über Ethernet**. Deshalb immer die **Ethernet-Adresse** des Ultimate eintragen
  (die Netzwerksuche nimmt sie automatisch).
- Ein Ultimate streamt immer nur an **ein** Ziel. Kommt 1 s lang kein Paket an – egal von wem –, fragt der Empfänger
  erneut, höchstens alle 10 s.
- Adressen dürfen Namen sein (auch `.local`); der Pico löst sie alle 60 s neu auf.
- Der Empfänger nimmt Pakete von **einer** Quelle: Kommen zwei Streams gleichzeitig (z. B. Ultimate direkt und
  weitergeleitet), bleibt er beim ersten, bis dieser 0,2 s schweigt. Die abgewiesenen Pakete zählt „Fremde Quelle abgewiesen“.

### Weitergeleiteter Stream

Holt ein anderes Gerät den Stream vom Ultimate und reicht ihn hierher weiter – z. B. die **Raspberry-Pi-Jukebox** als
Stream-Verteiler –, im Tab „Einstellungen“ den Haken **„Stream kommt weitergeleitet“** setzen. Der Empfänger fragt das
Ultimate dann **nie** selbst nach dem Stream, sondern wartet auf die Pakete („warte auf den weitergeleiteten Stream“).

Ohne den Haken fragt er nach 1 s ohne Pakete selbst – und weil das Ultimate nur an ein Ziel streamt, nimmt er den Stream
damit dem Verteiler weg: Fernseher und Visualizer am Pi bleiben dann stumm, nur die Anlage spielt (beobachtet 2026-10-23,
als der Pi den Stream kurz verloren hatte). Die Gerätemeldung trägt dann `"sm":2` wie bei einem Visualizer im Modus
„weitergeleitet“.

## Mehrere Ultimates und Netzwerksuche

Im Tab **Einstellungen** lassen sich bis zu vier Ultimates eintragen. Die Quelle wählt man im Tab **Wiedergabe** oder
mit dem Taster (1 s halten = nächster eingetragener).

**„Netzwerk nach Ultimates durchsuchen“** fragt jede Adresse des Heimnetzes nach `/v1/info` und zählt jedes Gerät, das
sich als Ultimate meldet. Die Suche geht nur im Heim-WLAN (nicht am eigenen Zugangspunkt) und dauert gut eine Minute;
**der Ton kann dabei kurz aussetzen**. Jedes Gerät erscheint einmal, mit seiner Ethernet-Adresse; ist es auch per WLAN
erreichbar, steht die zweite Adresse dahinter. Ein **nur per WLAN** gefundenes Gerät ist rot markiert – von dort kommt
kein Stream. **„übernehmen“** trägt einen Treffer in den nächsten freien Platz ein; danach „Speichern und neu starten“.

## Weiterleitung an den Visualizer

Weil ein Ultimate nur an ein Ziel streamt, reicht der Empfänger jedes Paket unverändert weiter:

1. Im Tab **Einstellungen** „Jedes Paket unverändert weiterreichen an“ einschalten und den Visualizer eintragen,
   z. B. `c64viz.local` oder seine IP.
2. Wahlweise das **Passwort der Visualizer-Webseite** eintragen. Für die Titelmeldungen der PC-Jukebox ist es nur bei
   älterer Visualizer-Firmware nötig (siehe [Titelmeldungen](#titelmeldungen)).
3. Beim Visualizer bleibt „Audio-Stream vom Ultimate verwenden“ an. In der Einstellung **Unicast** fordert er den
   Stream nur an, solange keine Pakete kommen – und es kommen ja welche vom Empfänger. Steht er auf
   **„von einem anderen Pico weitergeleitet“**, fragt er den Ultimate nie selbst.

Der Statuskasten zeigt das Ziel, seine aufgelöste Adresse („Adresse noch nicht gefunden“, solange der Name nicht
aufgelöst ist) und die weitergeleiteten Pakete pro Sekunde. Pakete gehen **nie an den Absender zurück** (zwei
weiterleitende Geräte bilden so keine Schleife) und nie an den Empfänger selbst.

**Wenn ein anderes Gerät den Stream wegnimmt:** Hört der direkte Stream auf, obwohl der Empfänger ihn nicht beendet
hat, hat ein anderes Gerät ihn angefordert (oder der Ultimate ist aus). Passiert das zweimal in 2 Minuten, zeigt der
Statuskasten eine Warnung und der Empfänger fragt nur noch alle 60 s. Abhilfe: das andere Gerät hinter den Empfänger
hängen (Weiterleitung) oder den Empfänger bei ihm eintragen.

### Ultimate64 SID Jukebox am PC

Die PC-Jukebox holt sich für einige Funktionen (Spectrum Analyzer, „Play Audio Locally“, „Skip after silence“) selbst
den Stream und reicht ihn an *den* Pico weiter, der bei ihr eingetragen ist.

- Trägt man dort den **Audioempfänger** ein (`c64audio.local`), läuft die Kette Ultimate → PC → Empfänger → Visualizer.
  Der Statuskasten zeigt dann „Stream kommt weitergeleitet“. Beim Umschalten zwischen PC und direkt kann der Ton kurz aussetzen.
- Bleibt dort der Visualizer eingetragen, holt sich der Empfänger den Stream nach spätestens 10 s zurück – die
  PC-Funktionen bekommen dann keinen Ton mehr.
- Steht in der Jukebox „Use Multicast IP“ an, holt sie den Stream per Multicast und leitet nicht weiter; der Empfänger
  nimmt ihn nach höchstens 10 s zurück. Multicast-Streams kann der Empfänger nicht empfangen.

### Titelmeldungen

Die PC-Jukebox meldet den laufenden Titel an den eingetragenen Pico (`/jb/np`). Der Empfänger zeigt ihn im
Statuskasten („Läuft“) und reicht jede Meldung unverändert an das Weiterleitungsziel weiter, auf einem von zwei Wegen:

- **Ohne eingetragenes Visualizer-Passwort** geht die Meldung zusammen mit dem Ton die Kette entlang (`C64NP1` an den
  Stream-Port des Visualizers, siehe [Netzwerk-Protokolle](#netzwerk-protokolle)). Der Visualizer nimmt sie dort ohne
  Passwort an – ab seiner Firmware vom 18.10.2026; ältere Visualizer kennen diesen Weg nicht.
- **Mit Passwort** meldet sich der Empfänger an der Visualizer-Webseite an und schickt sie dorthin (geht auch mit
  älterer Visualizer-Firmware).

Kommt umgekehrt eine solche Meldung mit dem Stream beim Empfänger an (ein Visualizer vor ihm in der Kette), zeigt er
den Titel ebenfalls und reicht sie mit dem Ton weiter.

Die Zeile „Titel an den Visualizer“ zeigt:

| Anzeige | Bedeutung |
|---|---|
| gesendet (ohne Passwort) | mit dem Ton weitergeschickt; dahinter, wie viele Meldungen es waren |
| übergeben | beim Visualizer angekommen (Weg mit Passwort); dahinter, wie viele Meldungen schon übergeben wurden |
| Passwort falsch | das eingetragene Visualizer-Passwort stimmt nicht – die Meldung wird verworfen |
| Visualizer beschäftigt | die vorige Meldung ist noch nicht verarbeitet; neuer Versuch nach 0,3 s |
| nicht erreichbar / Fehler … | der Visualizer antwortet nicht bzw. mit einem Fehler |

Meldungen, die älter als 15 s sind, verwirft der Empfänger (die Jukebox wiederholt sie alle 30 s).

## Geräte im Netz

Jeder Audioempfänger und jeder Visualizer meldet sich alle 10 s im Heimnetz: wer er ist, welche Firmware er hat und
wie der Ton durch ihn fließt. Die Karte **Geräte im Netz** im Tab „Wiedergabe“ zeigt das für alle Geräte – dieselbe
Karte steht auf der Webseite der Visualizer.

- **Weg des Tons:** zum Beispiel „Elite II (192.168.1.168) → c64audio 250/s → c64viz 250/s“. Bekommt ein
  Weiterleitungsziel nichts, steht es rot am Ende der Kette. Verteilt eine
  Raspberry-Pi-Jukebox den Stream, verzweigt der Weg am Pi: „Elite II → c64pi 251/s → [c64audio 251/s, c64viz 251/s,
  c64viz2 250/s]“; ein Ziel des Pi, das nichts bekommt, steht dort rot. Der Pi erscheint in der Liste als
  „Raspberry Pi“, mit dem laufenden Titel.
- **Je Gerät:** Name (ein Klick öffnet seine Webseite), Art, Adresse, Firmware-Stand, ob und von wem es den Stream
  bekommt und wohin es weiterleitet. Beim Audioempfänger steht „gestoppt“ oder „wartet auf den Stream“, wenn kein Ton kommt.
- **Warnungen** (rot): ein Weiterleitungsziel, dessen Name nicht gefunden wird; ein Gerät, das seit über 15 s nichts
  mehr gemeldet hat; bei einem Visualizer eine eingetragene Weiterleitung, die nicht läuft, weil sein Stream aus ist.
- **Neu suchen** fragt alle Geräte sofort. Ein Gerät, das 35 s lang stumm ist (ausgeschaltet), verschwindet aus der
  Liste. Gemerkt werden bis zu 12 andere Geräte.

Die Meldungen gibt es nur im Heim-WLAN, nicht am eigenen Zugangspunkt. Geräte mit älterer Firmware melden sich nicht
und fehlen in der Liste – nach einem Update sind sie dabei. Jede Meldung nennt auch die passende Firmware-Datei
(`c64audio` bzw. `c64audio_debug`); damit kann das Skript `tools/update_all.py` des Visualizer-Projekts alle Geräte auf
einmal aktualisieren.

### Einrichten mit dem Assistenten eines Visualizers

Der Einricht-Assistent auf der Webseite eines Visualizers verkettet alle Geräte so, dass der Ton bei allen ankommt
(Ultimate → Audioempfänger → Visualizer …). Für den Empfänger öffnet er dessen Webseite mit den passenden Werten schon
eingetragen: Der Tab **Einstellungen** zeigt oben den Hinweis „Vom Einricht-Assistenten … vorbereitet“, die geänderten
Felder sind gelb umrandet.

- **Weiterleitung:** an das nächste Gerät der Kette (z. B. `c64viz.local`) – oder aus, wenn der Empfänger das letzte ist.
- **Ultimate:** Steht er schon in der Liste, wird diese Zeile genommen, sonst die nächste freie (sind alle vier belegt,
  die letzte; deren altes Passwort wird dann gelöscht).
- Beim **Speichern und neu starten** wird dieser Ultimate zugleich die **Quelle** und die **Wiedergabe eingeschaltet**.

Gespeichert wird erst mit „Speichern und neu starten“ und dem Passwort **dieses** Empfängers – vorher die Werte
prüfen. Sobald der Empfänger sich danach mit diesen Werten meldet (siehe [Geräte im Netz](#geraete-im-netz)), hakt der
Assistent ihn ab.

## Puffer, Takt und Aussetzer

Die Pakete kommen per WLAN nicht gleichmäßig an. Der Puffer gleicht das aus: Er hält immer etwa die eingestellte Zeit
(Standard 80 ms) plus 4 ms für den DAC vor. Mehr Puffer überbrückt längere WLAN-Lücken, der Ton kommt dann aber später
als das Bild des C64.

Im Tab **Wiedergabe** zeigt die Zeile über dem Balken den Füllstand und den Ausgabetakt; der weiße Strich im Balken ist
das Ziel. **ppm** ist die Abweichung von 47 983 Hz in Millionsteln: −17 ppm heißt, der Ultimate läuft 17 Millionstel
langsamer. Nach etwa einer Minute ist der Takt eingeregelt.

| Zähler | Bedeutung |
|---|---|
| Verloren (überbrückt) | ein Paket fehlte, als es dran war – durch Wiederholung verdeckt, kein Knacken |
| Zu spät gekommen | kam nach seiner Zeit und wurde verworfen |
| Doppelt | kam zweimal an |
| Leergelaufen | der Puffer war leer: WLAN-Lücke länger als der Puffer – ggf. Puffer vergrößern |
| Pakete wiederholt | so viele Pakete wurden durch Wiederholung ersetzt |
| Pakete still | so viele Pakete wurden durch Stille ersetzt (längere Lücke) |
| Pakete verworfen (zu voll) | nach einem Stau zu viel im Puffer: auf das Ziel zurückgeschnitten |
| Neustarts (Sequenzsprung) | neuer Stream oder andere Quelle: neu angefangen |
| Puffer neu gefüllt | wie oft der Puffer vor dem Abspielen neu gefüllt wurde |
| Fremde Quelle abgewiesen | Pakete eines zweiten Senders, während der erste lief |
| Stream von anderem Gerät übernommen | wie oft ein anderes Gerät den direkten Stream angefordert hat |
| Interne Engpässe | erscheint nur, wenn der Pico selbst nicht hinterherkam (sollte 0 bleiben) |

Einzelne verlorene Pakete sind normal und nicht zu hören. Steigt „Leergelaufen“ regelmäßig, den Puffer vergrößern
(z. B. auf 120 ms) oder den Empfang verbessern (Pico näher an den Router).

## Sicherung, Update und Passwort

### Sicherung

Tab **System → „Sicherung herunterladen“** speichert alle Einstellungen in einer Datei `c64audio-<name>-<datum>.json`:
WLAN, Webseiten-Passwort, Gerätename, die Ultimate-Geräte, Weiterleitung, Puffer, Lautstärke und Quelle.
**Die Datei enthält Passwörter** (WLAN, Ultimate, Visualizer) – sicher aufbewahren.

„Sicherung einspielen …“ fragt nach, schreibt die Datei zurück und startet den Pico neu – praktisch für einen zweiten
Empfänger oder nach `wlan_reset.uf2`. Der Pico prüft vorher alles; eine beschädigte Datei oder eine Sicherung des
Visualizers wird abgelehnt und ändert nichts. Hat die Sicherung ein anderes Webseiten-Passwort oder einen anderen
Gerätenamen, gilt danach der aus der Sicherung.

### Firmware-Update über die Webseite

Tab **System → „Firmware (.uf2) wählen …“** und die neue `c64audio.uf2` auswählen. Die Seite zeigt die laufende Firmware
(Bau-Datum) und prüft die Datei schon im Browser: Eine Firmware des Visualizers oder eine zu große Datei wird
abgelehnt. Die Wiedergabe hält während des Updates an. Der Pico legt die Firmware zuerst in einem freien Bereich ab,
prüft Übertragung und Inhalt und kopiert sie erst dann über sich selbst; danach startet er neu (insgesamt etwa 15 s).
Einstellungen bleiben erhalten.

**Während des Aufspielens nicht den Strom trennen.** Geht doch etwas schief, startet der Pico im USB-Laufwerksmodus –
dann die `.uf2` per USB kopieren (Taster beim Einstecken halten oder BOOTSEL).

Mögliche Meldungen: „Prüfsumme falsch – Übertragung fehlerhaft“ (nochmal versuchen), „keine Firmware des
C64-Audioempfängers“, „Firmware zu groß oder leer“, „Pico ist beschäftigt“ (gleich nochmal versuchen).

### Passwort vergessen

`wlan_reset.uf2` auf den Pico kopieren (BOOTSEL bzw. Taster beim Einstecken halten). Sie löscht nur die WLAN-,
Ultimate- und Passwort-Einstellungen; Firmware, Lautstärke und Quelle bleiben. Danach öffnet der Pico wieder seinen
Zugangspunkt **C64-Audio** (Passwort `commodore64`), wo die Seite kein Passwort braucht. Eine vorher gespeicherte
[Sicherung](#sicherung) bringt alles zurück. (Es ist dieselbe Datei wie beim Visualizer.)

## WLAN-Ausfälle und Verbindungsaufbau

Wie der Pico beim Verbinden reagiert, hängt davon ab, warum es nicht klappt:

| Lage | Verhalten |
|---|---|
| WLAN-Passwort falsch | sofort eigener Zugangspunkt – dort das Passwort korrigieren |
| beim Router angemeldet, aber keine Adresse (DHCP) | bis 30 s auf die Adresse warten, dann ab- und neu anmelden |
| WLAN nicht gefunden, Verbindung fehlgeschlagen, 20 s ohne Fortschritt | neu verbinden (schnelle Fehlschläge höchstens alle 3 s) |
| drei Versuche ohne Erfolg | eigener Zugangspunkt **C64-Audio**; von dort alle 5 Minuten erneut ins Heim-WLAN – nicht, solange jemand die Seite am Zugangspunkt benutzt |
| Heim-WLAN lief seit dem Einschalten schon | nur neu verbinden, nie eigener Zugangspunkt (z. B. Router startet neu) |
| Verbindung im Betrieb verloren | nach 10 s neu verbinden |
| verbunden, aber nichts kommt durch (3 Anfragen an den Ultimate ohne Antwort, kein Stream, 2 min keine Webseite) | ab- und neu anmelden, höchstens alle 5 Minuten |

Jede dieser Entscheidungen steht im [Diagnose-Protokoll](#diagnose-protokoll), z. B.
`wlan: keine Adresse per DHCP (Versuch 1) - verbinde neu`.

## Fehlersuche

| Problem | Ursache / Abhilfe |
|---|---|
| Kein Ton, Statuskasten „spielt“ | Lötbrücke **H3L (XSMT) auf H**? SCK an GND? Kabel an DIN/BCK/LCK richtig (GP20/21/22)? Eingang an der Anlage gewählt? |
| Kein Ton, „Stream kommt nicht an“ | Statuszeile „Ultimate“ lesen; Adresse = **Ethernet**-Adresse des Ultimate? Netzwerk-Passwort richtig? |
| „Passwort falsch (403)“ | Netzwerk-Passwort des Ultimate stimmt nicht |
| „Adresse nicht gefunden“ | Name falsch oder nicht auflösbar – IP-Adresse eintragen |
| Ton verzerrt, zu laut | Lautstärke senken (50–80 %); an einen Line-Eingang, nicht PHONO |
| Brummen | Masseschleife: Ladegerät ohne Schutzkontakt oder USB der Anlage |
| kurze Aussetzer | „Leergelaufen“ steigt: Puffer vergrößern, Pico näher an den Router; während einer Netzwerksuche normal |
| Ton setzt alle paar Sekunden aus, Warnung „fordert den Stream immer wieder an“ | ein anderes Gerät (Visualizer, PC-Jukebox) holt sich den Stream – [Weiterleitung](#weiterleitung-an-den-visualizer) einrichten |
| Visualizer zeigt nichts | Weiterleitung eingeschaltet? Ziel aufgelöst (Statuskasten)? Beim Visualizer der Stream an? |
| „Titel an den Visualizer: Passwort falsch“ | das Passwort der Visualizer-Webseite eintragen |
| `c64audio.local` geht nicht | IP aus dem Router nehmen; manche Android-Geräte kennen `.local` nicht; anderer Gerätename vergeben? |
| Pico öffnet seinen Zugangspunkt | WLAN-Passwort falsch oder Heim-WLAN nicht erreichbar – siehe [WLAN-Ausfälle](#wlan-ausfaelle-und-verbindungsaufbau) |
| Webseiten-Passwort vergessen | [Passwort vergessen](#passwort-vergessen) |

### Diagnose-Protokoll

Der Pico schreibt mit, was er tut: WLAN, jede Anfrage an den Ultimate, den Stream, die Titelmeldungen, die Netzwerksuche
und Firmware-Updates. Anzeigen: Tab **System → „Diagnose laden“** oder direkt `http://c64audio.local/diag`. Jede Zeile
beginnt mit den Sekunden seit dem Start; die ältesten Zeilen fallen heraus, wenn der Speicher voll ist. Die
Debug-Firmware gibt dasselbe live über USB-Seriell aus (115200 Baud, z. B. `screen /dev/tty.usbmodem* 115200`).

| Zeile | Bedeutung |
|---|---|
| `start: C64-Audioempfänger, Firmware …` | Pico gestartet, mit Bau-Datum |
| `net: init - ssid=… quelle=… weiterleiten=… puffer=… ms` | die geladenen Einstellungen |
| `net: WLAN-Einstellungen des Visualizers übernommen` | auf dem Pico lagen Einstellungen des Visualizers |
| `wlan: Verbindungsstatus a -> b` | Zwischenschritte: 1 = verbindet, 2 = angemeldet, wartet auf Adresse, 3 = verbunden; −1 = fehlgeschlagen, −2 = WLAN nicht gefunden, −3 = Passwort falsch |
| `wlan: verbunden mit '…', IP …` | im Heimnetz angemeldet |
| `wlan: <Grund> (Versuch n) - verbinde neu` | Grund: „keine Adresse per DHCP“, „WLAN nicht gefunden“, „Verbindung fehlgeschlagen“, „keine Antwort vom Router“ |
| `wlan: <Grund> (Versuch n) - eigener Zugangspunkt` | aufgegeben, Zugangspunkt geöffnet |
| `wlan: eigener Zugangspunkt C64-Audio (192.168.4.1)` | Zugangspunkt läuft |
| `wlan: versuche das Heim-WLAN erneut` | der 5-Minuten-Versuch vom Zugangspunkt aus |
| `wlan: Verbindung verloren - verbinde neu` | WLAN im Betrieb weg |
| `wlan: keine Antworten im Netz - verbinde neu` | verbunden, aber nichts kam durch |
| `net: mDNS als c64audio.local` | unter diesem Namen erreichbar |
| `rest: PUT /v1/streams/audio:start?… an …` | Anfrage an den Ultimate |
| `rest: Antwort 200 (start)` | der Ultimate streamt; `403` = Netzwerk-Passwort falsch |
| `rest: keine Antwort nach 10 s - abgebrochen` | der Ultimate antwortet nicht (aus, falsche Adresse) |
| `rest: Verbindungsfehler err=…` | der Ultimate ist nicht erreichbar |
| `player: Wiedergabe an` / `aus`, `player: Quelle n (…)` | Start/Stopp, Quellenwechsel |
| `stream: kommt nicht mehr vom Ultimate (n. Mal)` | ein anderes Gerät hat den Stream geholt |
| `titel: Visualizer lehnt das Passwort ab - Meldung verworfen` | Visualizer-Passwort falsch |
| `titel: Meldung zu alt - verworfen` | Titelmeldung kam nicht rechtzeitig an |
| `scan: … -> …` | ein Gerät der Netzwerksuche |
| `ota: beginne …`, `ota: … Bytes geprüft`, `ota: kopiere …` | Ablauf eines Firmware-Updates; `ota: <Fehler>` bei Ablehnung |
| `net: Sicherung eingespielt, Neustart` | eine Sicherung wurde zurückgeschrieben |

## Anhang

### Anschlüsse am Pico

| Funktion | Pin |
|---|---|
| PCM5102A DIN | GP20 (Pin 26) |
| PCM5102A BCK | GP21 (Pin 27) |
| PCM5102A LCK | GP22 (Pin 29) |
| PCM5102A VIN / GND / SCK | VSYS (Pin 39) / GND (Pin 38) / GND |
| Taster (optional) | GP15 (Pin 20) gegen GND |
| LED | die LED auf dem Pico 2 W |

Die Pins sind so gewählt, dass sie nicht mit denen des Visualizers kollidieren.

### HTTP-Schnittstelle (für eigene Programme)

Im Heimnetz brauchen alle Anfragen die Digest-Anmeldung mit dem Webseiten-Passwort (Benutzername beliebig); am eigenen
Zugangspunkt nicht.

| Anfrage | Bedeutung |
|---|---|
| `GET /status` | Zustand als JSON (alles aus Statuskasten und Wiedergabe-Tab); `"u64"` ist die Adresse der aktuellen Quelle |
| `POST /cmd?c=play` | Start; ebenso `stop` |
| `POST /cmd?c=vol&v=60` | Lautstärke in Prozent (0–100) |
| `POST /cmd?c=src&i=1` | Quelle auf den zweiten Ultimate stellen (0–3) |
| `POST /cmd?c=reset` | Zähler zurücksetzen |
| `POST /config` | Einstellungen speichern (Formularfelder wie im Tab „Einstellungen“, wahlweise `src=0…3` als Quelle und `play=1`), danach Neustart |
| `GET /u64/scan`, `POST /u64/scan` | Netzwerksuche abfragen bzw. starten (nur im Heim-WLAN) |
| `POST /jb/np?new=1&title=…&author=…` | „Now Playing“ eines PC-Programms melden; `POST /jb/np?stop=1` am Ende |
| `GET /backup`, `POST /restore` | Sicherung als JSON lesen bzw. einspielen |
| `GET /fw/state`, `POST /fw/begin` … `/fw/apply` | Firmware-Update (so, wie es die Webseite macht) |
| `GET /diag` | Diagnose-Protokoll als Text |
| `GET /hilfe` | dieses Handbuch |
| `GET /devices` | Geräte im Netz als JSON (`{"list":[{"self":true,"age":0,"j":"<Meldung>"},…]}`, dieses Gerät zuerst) |
| `POST /devices/query` | alle Geräte sofort fragen |

### Netzwerk-Protokolle

Alles per UDP im Heimnetz, ohne Passwort (wie der Audio-Stream des Ultimate selbst). Dasselbe Format wie beim Visualizer.

| Port | Inhalt |
|---|---|
| 11001 | Audio-Stream (vom Ultimate oder weitergeleitet). Zusätzlich `C64NP1 <Abfrage>`: eine Titelmeldung der PC-Jukebox, die die Kette entlang weitergereicht wird – dieselben Parameter wie `POST /jb/np` |
| 11064 | Gerätemeldungen: `C64DEV1 {JSON}` alle 10 s (±0,5 s) an alle (Broadcast 255.255.255.255), `C64DEV1?` fragt alle, jedes Gerät antwortet nach 50–400 ms; beim Start fragt jedes Gerät einmal |

Felder der Meldung des Audioempfängers: `t` Art (`audio`), `n` Name, `ip`, `b` Firmware-Stand (wie `GET /fw/state`),
`img` passende Firmware-Datei ohne `.uf2` (`c64audio` oder `c64audio_debug`), `up` Sekunden seit dem Start, `st` 1 = Wiedergabe an
(der Stream wird angefordert), `rx` empfangene Pakete/s, `from` Absender des Streams (leer, wenn keiner kommt), `fw`
Weiterleitungsziel (leer, wenn aus), `fwip` seine Adresse, `fp` weitergeleitete Pakete/s, `src`/`srcl` Adresse und
Name des gewählten Ultimate. Visualizer melden zusätzlich `sm` (Empfänger-Modus) und die Felder für zwei Matrizen.
