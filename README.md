# Mix 'n Smash

A colour-mixing reaction game on an LED strip. Coloured dots race down a strip
of about 300 LEDs towards the player, who has three buttons: **red**, **yellow**
and **blue**. Hit each dot with the colour it is made of before it reaches you.

- A **primary** dot (red, yellow, blue) is neutralised by its own colour.
- A **mixed** dot needs both of its colours: **orange** = red + yellow,
  **purple** = red + blue, **green** = yellow + blue. Hit an orange dot with
  yellow and a red dot remains, which a red shot then neutralises.
- From level 5, **white** dots need all three colours.
- A **wrong colour** (blue on orange, say) or a dot that **reaches you** costs a
  life. You start with three; the first three LEDs show them.
- Every level has a fixed number of dots. Clear them and you get a rainbow, a
  life back, and a faster level with more mixed colours and more dots at once.
  The speed also creeps up inside a level, so it never jumps.

The same rules run in three places from one source:

| Where | What |
|---|---|
| [`game/`](game/) | The rules in portable C, integer-only and deterministic. Compiled into the firmware and into the tests. |
| [`web/`](web/) | The browser version (arrow keys **←** red, **↓** yellow, **→** blue) and the web interface of the ESP32: high scores, player name, live view of the strip. [`game.js`](web/game.js) is a line-by-line port of `game.c`. |
| [`firmware/`](firmware/) | ESP-IDF / PlatformIO project for a Wemos ESP-WROOM-32 driving a WS2812B strip, three buttons, Wi-Fi and the web page. |

[`tests/`](tests/) holds unit tests for the rules and a trace test that runs
scripted games through both the C and the JavaScript version and checks that
every LED of every frame is identical.

## Play it in the browser

Open [`web/index.html`](web/index.html) in a browser (double-click works, no
server needed). Arrow keys or the on-screen buttons play; the simulated strip
defaults to 300 LEDs. Scores stay in the browser's local storage.

## Hardware

| Part | Notes |
|---|---|
| Wemos ESP-WROOM-32 | Any classic ESP32 board with 4 MB flash works (`board = esp32dev`). |
| WS2812B LED strip, ~300 LEDs | 5 V. Data from **GPIO 16** through a 300-500 Ω resistor; a 74AHCT125 level shifter helps with long wires. Inject 5 V at both ends of a 5 m strip. |
| 3 push buttons | **Red GPIO 25**, **yellow GPIO 26**, **blue GPIO 27**, each to GND. The internal pull-ups are used. GPIO 34-39 have no pull-ups, avoid them. |
| 5 V power supply | See *Power* below. |

The player sits at the start of the strip (LED 0, nearest the data input).
If your strip is the other way around, turn on *Player sits at the far end* in
menuconfig. All pins, the LED count and the brightness are menuconfig settings.

### Power

Three hundred WS2812Bs can draw 18 A at full white, which the game never
needs: nothing ever lights the whole strip. Feedback (lost life, game over,
countdown) stays within about 20 LEDs next to the player, celebrations are
short comets, and only a handful of dots and shots are on the strip at once.
In practice the strip draws a few hundred milliamps.

Two settings keep it safe anyway:

- **Brightness** (default 80 of 255, after gamma correction) scales everything.
- **Current limit** (default 1500 mA): every frame the firmware estimates the
  current the strip would draw (1 mA idle per LED plus 20 mA per fully lit
  colour) and dims the whole frame when the estimate is over the limit. Set it
  below what your supply can deliver. Note that 300 LEDs draw about 300 mA
  when they are all off.

The web page shows the current estimate live.

## Build and flash

PlatformIO (from `firmware/`, or with the toolbar in VS Code after opening
`mix-n-smash.code-workspace`):

```sh
cd firmware
pio run -t upload -t monitor
```

ESP-IDF v5.4 or newer:

```sh
. $IDF_PATH/export.sh
cd firmware
idf.py build flash monitor
```

The first build downloads the toolchain and two components from the ESP
component registry (`espressif/led_strip`, `espressif/mdns`). The web page is
compiled into the firmware from `firmware/main/web_page.h`; after changing
anything in `web/`, regenerate it:

```sh
python3 tools/build_web.py
```

### Settings (`pio run -t menuconfig` or `idf.py menuconfig` → *Mix 'n Smash*)

| Setting | Default |
|---|---|
| LED strip → Number of LEDs | 300 (16-400) |
| LED strip → Data GPIO | 16 |
| LED strip → Player sits at the far end | off |
| LED strip → Strip expects RGB instead of GRB | off |
| LED strip → Brightness | 80 |
| LED strip → Current limit | 1500 mA |
| Buttons → Red / yellow / blue GPIO | 25 / 26 / 27 |
| Buttons → Buttons connect to 3.3 V instead of GND | off |
| Game → A dot hit by the wrong colour keeps coming | off (stricter variant) |
| Game → Frame period | 16 ms |
| Wi-Fi → Network to join / password | empty: start an access point |
| Wi-Fi → Access point name / password | `mix-n-smash`, open |
| Wi-Fi → Hostname | `mix-n-smash` |

## Web interface

With no Wi-Fi network configured the ESP32 starts an open access point called
`mix-n-smash`; connect to it and open <http://192.168.4.1>. With a network
configured it joins that network and announces itself as
`http://mix-n-smash.local` (the IP is also printed on the serial monitor). If
joining fails six times it falls back to the access point.

The page shows the strip live, the level, lives and score, and the top-10
table. Enter a name and the next games on the strip are recorded under it.
The on-screen buttons and arrow keys press the real buttons, which is handy
for testing without wiring. *Speel hier in de browser* switches the same page
to the simulated strip.

API, for anything else you want to build on it:

| Method | Path | |
|---|---|---|
| GET | `/api/state` | state, level, lives, score, player, best, `leds` as hex |
| GET | `/api/highscores` | top 10 and the current player |
| POST | `/api/player` | `{"name": "Bart"}` |
| POST | `/api/press` | `{"color": "red"}` (or `yellow`, `blue`) |
| DELETE | `/api/highscores` | clear the table |

## Levels

| Level | Dots | Time to cross | Mixed | White | At once | Pause between dots |
|---|---|---|---|---|---|---|
| 1 | 8 | 7.0 s | 0 % | 0 % | 1 | 1.5-2.5 s |
| 2 | 10 | 6.0 s | 25 % | 0 % | 2 | 1.3-2.3 s |
| 3 | 12 | 5.2 s | 40 % | 0 % | 2 | 1.1-2.0 s |
| 4 | 14 | 4.6 s | 50 % | 0 % | 3 | 0.95-1.8 s |
| 5 | 16 | 4.0 s | 55 % | 10 % | 3 | 0.85-1.6 s |
| 6 | 18 | 3.5 s | 60 % | 15 % | 4 | 0.75-1.4 s |
| 7 | 20 | 3.1 s | 65 % | 20 % | 4 | 0.65-1.2 s |
| 8 | 22 | 2.8 s | 70 % | 25 % | 5 | 0.6-1.1 s |
| 9 | 24 | 2.5 s | 70 % | 30 % | 5 | 0.55-1.0 s |
| 10+ | 26 | 2.2 s | 75 % | 35 % | 6 | 0.5-0.9 s |

"Time to cross" is how long a dot takes from the far end to the player, so
the game feels the same on a 60-LED and a 300-LED strip. Within a level it
slides towards the next level's value. From level 5 dots also vary in speed
(±10 % to ±25 %). Shots cross the strip in 0.9 s. Points: 10 per colour in
the dot times the level, plus 50 × level + 25 × lives left when a level is
cleared. Past level 10 the parameters stay at the level-10 values.

The table is `LEVELS` in [`game/game.c`](game/game.c) and
[`web/game.js`](web/game.js); keep the two identical (the trace test fails
otherwise).

## Tests

```sh
make -C tests check
```

`test` builds and runs the unit tests (`gcc`); `trace` runs four scripted
games through the C core and through `web/game.js` (`node`) and compares the
traces line by line. Both run in CI, together with a PlatformIO build, an
ESP-IDF v5.4 build and a check that `web_page.h` matches `web/`.

## Layout

```
game/        game.h, game.c   the rules; also an ESP-IDF component
web/         index.html, game.js   browser version and web interface
firmware/    PlatformIO + ESP-IDF project (main/ has the sources)
tests/       unit tests, trace comparison, Makefile
tools/       build_web.py   bundles web/ into firmware/main/web_page.h
```
