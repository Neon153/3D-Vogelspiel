# Vogelflug

Als Vogel über eine verlassene Stadt fliegen, die sich die Natur zurückholt.
Eigene C++/Vulkan-Engine, das komplette Bild entsteht per Hardware-Raytracing (`VK_KHR_ray_query`).

![Flug über die Stadt](screenshots/1_vogel_stadt.png)

| | |
|---|---|
| ![Häuserschlucht](screenshots/4_haeuserschlucht.png) | ![Glastürme](screenshots/5_glastuerme.png) |
| ![Vogel](screenshots/2_vogel_aufschlag.png) | ![Nacht](screenshots/7_nacht.png) |

Die Bilder stammen aus der Engine, gerendert mit einem Software-Treiber in 720p bzw. 540p.

## Starten

`Vogelflug.exe` (liegt fertig gebaut im Hauptverzeichnis dieses Repos) herunterladen und doppelklicken. Es gibt keine Installation und keine Zusatzdateien: Shader, Schrift und Welt-Generator stecken in der .exe, die Welt wird beim Start in ein paar Sekunden erzeugt.

Voraussetzungen: Windows 10/11 (64 Bit), Grafikkarte mit Hardware-Raytracing und aktuellem Treiber (entwickelt für RTX 5060 Ti 16 GB; jede RTX-Karte bzw. RX 6000+ / Arc sollte gehen). Es werden rund 2 GB Grafikspeicher belegt.

Falls etwas nicht startet: Im selben Ordner entsteht `vogelflug.log`, bei Fehlern erscheint zusätzlich ein Meldungsfenster.

## Steuerung

| Taste | Funktion |
|---|---|
| W / S | schneller / langsamer |
| A / D | nach links / rechts drehen |
| Maus | Richtung ändern, steigen und sinken |
| Q / E, Mausrad | Tageszeit zurück / vor (jederzeit) |
| 1 – 6 | Morgen, Vormittag, Mittag, Abend, Dämmerung, Nacht |
| T | Zeitablauf an / aus |
| + / − (auch Bild↑ / Bild↓) | Zeitablauf schneller / langsamer |
| N / M | weniger / mehr Wolken |
| C | Kamera: nah, weit, Ich-Perspektive |
| F1 – F4 | Qualität: Leistung, Ausgewogen, Hoch, Ultra |
| V | VSync an / aus (aus = maximale FPS) |
| F11 | Vollbild / Fenster |
| H, Tab | Hilfe, Anzeige ausblenden |
| R | zurück zum Startpunkt |
| Esc | Pause (dann X zum Beenden) |

Im Sturzflug wird der Vogel schneller, im Steigflug langsamer. Gebäude und Boden sind fest: der Vogel gleitet an Wänden entlang. Durch Baumkronen kann man hindurchfliegen.

## Was die Grafik macht

- **Raytracing für alles**: Primärstrahlen, Schatten von Sonne und Mond (weich, über die Größe der Scheibe gestreut), 1-Bounce Global Illumination, Spiegelungen auf Wasser, Fensterglas und Pfützen, volumetrischer Nebel mit Lichtschäften.
- **Tag/Nacht**: Sonnen- und Mondbahn für 48,7° Nord, physikalisch basierter Himmel (Rayleigh/Mie/Ozon), Sonnenfarbe aus der atmosphärischen Transmission, Morgennebel, Sterne, Wolken mit Wolkenschatten, automatische Belichtung.
- **Entrauschen**: zeitliche Akkumulation mit Reprojektion, À-trous-Filter, TAA, danach Bloom und ACES-Tonemapping.
- **Welt**: prozedural, rund 4,8 Mio. Dreiecke und 125 000 Instanzen. Stadt mit Hochhäusern, Ruinen, Efeu, Dachbewuchs, Autowracks und Laternen, Wald im Westen, Fluss mit eingestürzter Brücke im Süden.
- **Vogel**: ein einziges zusammenhängendes Netz (34 000 Dreiecke), aus einer geglätteten Distanzfunktion erzeugt. Für den Flügelschlag wird es pro Frame an Schulter, Handgelenk und Federspitzen gebogen und seine Raytracing-Struktur neu gebaut.
- **Gebäude**: L-förmige und gestaffelte Baukörper, Fassadenrelief (Lisenen, Geschossbänder, Gesimse, Fensterbänke, Balkone, Feuertreppen, Markisen), Satteldächer, Wassertanks, Klimageräte und Technik-Kronen. Fenster sitzen mit Parallaxe in der Wand und zeigen Innenräume (Interior Mapping). Glasfassaden spiegeln je nach Beschichtung unterschiedlich stark und farbig, jede Scheibe ist leicht verkippt und gewölbt.

Startoptionen (Verknüpfung oder Konsole): `--fenster`, `--size 1920 1080`, `--quality 0..3`, `--density 0.5` (weniger Vegetation), `--time 18.5`, `--novsync`.

## Stand der Tests

- Unter Linux mit dem Software-Vulkan-Treiber lavapipe gerendert, mit den Khronos-Validation-Layern geprüft. Einzige Meldung dort: der Vertexpuffer (263 MB) überschreitet das lavapipe-Limit von 128 MB; bei NVIDIA liegt dieses Limit bei 4 GB.
- Die Windows-.exe lief unter Wine (ebenfalls lavapipe) bis zum fertigen Bild.
- Kollisionstest: 1,5 Mio. Simulationsschritte, der Vogel landet nie in einem Gebäude.
- **Nicht getestet: echte RTX-Hardware.** Bildrate und Treiberverhalten auf der 5060 Ti sind daher ungemessen. Bei zu wenig FPS F2 oder F1 drücken.

## Selbst bauen

Linux (Ubuntu 24.04), erzeugt Linux-Binary und Windows-.exe per MinGW:

```
sudo apt install g++ cmake git glslang-tools python3-pil g++-mingw-w64-x86-64-posix \
                 libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
./setup_deps.sh        # volk, Vulkan-Headers, GLFW 3.4 nach ../deps
./build.sh all         # -> build/vogelflug und dist/Vogelflug.exe
```

Die .exe im Hauptverzeichnis ist eine Kopie von `dist/Vogelflug.exe` aus genau diesem Quellstand.

```
# Kollisionstest:
g++ -std=c++17 -O2 tools/test_collide.cpp src/world.cpp -o build/test_collide && ./build/test_collide
```

Unter Windows geht dasselbe in einer MSYS2-MinGW64-Shell (`glslang`, `python`, `gcc`, `glfw`), `build.sh` braucht dann nur angepasste Compiler-Namen.

## Dateien

| Datei | Inhalt |
|---|---|
| `src/main.cpp` | Vulkan-Engine, Beschleunigungsstrukturen, Render-Passes, Flugmodell, Eingabe, HUD |
| `src/world.cpp`, `world.h` | Welt-Generator (Stadt, Ruinen, Vegetation, Vogel), Kollision |
| `src/mathx.h` | Vektoren, Noise (bitgleich zur GLSL-Version) |
| `src/shaders.h`, `font.h` | generiert: eingebettete SPIR-V-Shader und HUD-Schrift |
| `shaders/rt.comp` | Raytracing: Materialien, Beleuchtung, Spiegelungen, Nebel |
| `shaders/sky.comp` | Atmosphärenmodell |
| `shaders/temporal.comp`, `atrous.comp`, `composite.comp`, `taa.comp` | Entrauschen und Zusammensetzen |
| `shaders/bloom.comp`, `exposure.comp`, `final.*` | Bloom, Belichtung, Tonemapping, HUD |
| `tools/` | Shader-Einbettung, Font-Erzeugung, Kollisionstest, Testbild-Skript |

## Entstehung: die Prompts

Das Spiel ist komplett im Chat mit Claude (Anthropic) entstanden. Hier stehen alle Prompts, die dafür nötig waren, wörtlich und unverändert übernommen (inklusive Tippfehlern). Die Prompts geben nur das Ziel vor. Die technischen Entscheidungen hat Claude getroffen: eigene Vulkan-Engine statt Game-Engine, Ray Queries, prozedurale Welt, Cross-Compile der .exe unter Linux und die Tests mit einem Software-Treiber.

### 1. Das Spiel

> Erstelle ein 3D Spiel für den PC das auf einer RTX5060TI 16GB läuft mit Raytracing läuft. Das Spiel soll folgendes können, man ist ein Vogel und kann über eine Stadt fliegen in der keine Menschen mehr Leben diese Stadt ist komplkett verlassen. Die Natur hat angfangen sich die Stadt zurück zu holen. Mit WASD soll es möglich sein den Vogel zu steuern W beschleunigen, A links, D rechts, S verlangsamen mit der Maus soll es möglich sein höher oder tiefer zu fliegen oder gar auch die richtung wechseln. Das Spiel soll ein tages nacht rythmus haben wobei man die gewünschte Tageszeit immer einstellen kann. Es soll möglich sein mit dem Vogel über die Stadt bzw. auch zwischen den Häusern zu fliegen. Die Sonne und der Mond sollen Licht mit Raytracing ausgeben. Auf der einen seite der Stadt soll noch ein Wald sein. Erstelle das Spiel mit einer sehr realistischen Grafik. Gebe den alle Datein die zu, Spiel gehören aus und eine exe mit der man das Spiel sofort auf Windows starten kann

Ergebnis: die erste Version mit der C++/Vulkan-Engine (komplettes Bild per `VK_KHR_ray_query`), physikalischem Himmel, Sonnen- und Mondlicht, Global Illumination, Spiegelungen und Nebel. Dazu kamen eine prozedurale Stadt mit Ruinen und Bewuchs, der Wald im Westen, der Fluss mit eingestürzter Brücke, Flugsteuerung mit Kollision und ein Tag/Nacht-Zyklus mit einstellbarer Uhrzeit. Die Windows-.exe ist per MinGW gebaut und unter lavapipe und Wine getestet.

### 2. Gebäude

> Kannst du die Gebäude realistischer machen also bisschen mehr nach gebäude ausseh lassen, gerade sind das eher leblose blöcke, bisschen mehr dynamic in sachen reflektion muss rein. lass alles andere so wie es ist und ändere nur die gebäude

Ergebnis:
- **Baukörper:** L-förmig und gestaffelt.
- **Fassadenrelief:** Lisenen, Gesimse, Fensterbänke, Balkone, Feuertreppen und Markisen.
- **Dächer:** Satteldächer, Wassertanks, Klimageräte und Technik-Kronen.
- **Fenster:** mit Parallaxe in die Wand gesetzt, dahinter Innenräume (Interior Mapping).
- **Glas:** Glasfassaden spiegeln je Gebäude unterschiedlich stark und getönt, jede Scheibe ist leicht verkippt.

Straßennetz, Grundstücke und Vegetation sind dabei unverändert geblieben.

### 3. Vogel

> mach den vogel zu einem stück anstatt in ein eckiges zusammen gesetztes modell

Ergebnis: Der Vogel ist jetzt ein einziges zusammenhängendes Netz statt fünf starr gedrehter Einzelteile. Das Netz wird aus einer geglätteten Distanzfunktion erzeugt und für den Flügelschlag pro Frame an Schulter, Handgelenk und Federspitzen gebogen. Dazu kam ein neues Gefieder mit Bänderung, dunklen Flügelspitzen und gelbem Schnabel.

### 4. Veröffentlichung

> https://github.com/Neon153/3D-Vogelspiel
>
> Lade alles auf Github hoch

> Füge zur Doku noch die relevanten Prompts hinzu die zum Ziel geführt haben und push es auf github

Ergebnis: dieses Repository und dieser Abschnitt.

## Lizenzen der Abhängigkeiten

GLFW (zlib), volk (MIT), Vulkan-Headers (Apache 2.0 / MIT), HUD-Schrift aus DejaVu Sans Mono (Bitstream-Vera-Lizenz). GLFW und volk sind statisch in die .exe gelinkt.
