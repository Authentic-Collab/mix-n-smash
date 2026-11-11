# Mix 'n Smash

A colour-mixing reaction game on an LED strip. Coloured dots race down a strip
of about 300 LEDs towards the player, who has three buttons: **red**, **yellow**
and **blue**. Hit each dot with the colour it is made of before it reaches you.

- A **primary** dot (red, yellow, blue) is neutralised by its own colour.
- A **mixed** dot needs both of its colours: **orange** = red + yellow,
  **purple** = red + blue, **green** = yellow + blue. Hit an orange dot with
  yellow and a red dot remains, which a red shot then neutralises.
- From level 5, **white** dots need all three colours. White is shown a bit
  dimmer (200/200/200) because it lights all three LED channels.
- A **wrong colour** (blue on orange, say) or a dot that **reaches you** costs a
  life. You start with three; the first three LEDs show them.
- Every level has a fixed number of dots. Clear them and you get a rainbow, a
  life back, and a faster level with more mixed colours and more dots at once.
  The speed also creeps up inside a level, so it never jumps.

The same rules run in three places from one source:

| Where | What |
|---|---|
| [`game/`](game/) | The rules in portable C, integer-only and deterministic. Compiled into the firmware and into the tests. |
| [`web/`](web/) | One page with four views: watch the strip live, play from a phone, practice in the browser (arrow keys **←** red, **↓** yellow, **→** blue), and the settings. [`game.js`](web/game.js) is a line-by-line port of `game.c`. |
| [`firmware/`](firmware/) | ESP-IDF / PlatformIO project for a Wemos ESP-WROOM-32 driving a WS2812B strip, three buttons, optional lights in the buttons, Wi-Fi and the web page. |

[`tests/`](tests/) holds unit tests for the rules and a trace test that runs
scripted games through both the C and the JavaScript version and checks that
every LED of every frame is identical.

## Play it in the browser

Open [`web/index.html`](web/index.html) in a browser (double-click works, no
server needed): it starts in practice mode with a simulated strip of 300
LEDs. Arrow keys or the on-screen buttons play; the number of LEDs, the
speed and the dots per level can be changed. Scores stay in the browser.

## Hardware

| Part | Notes |
|---|---|
| Wemos ESP-WROOM-32 | Any classic ESP32 board with 4 MB flash works (`board = esp32dev`). |
| WS2812B LED strip, ~300 LEDs | 5 V. Data from **GPIO 16**; keep that wire short. Inject 5 V at both ends of a 5 m strip when you raise the brightness. |
| 3 push buttons | **Red GPIO 25**, **yellow GPIO 26**, **blue GPIO 27**, each to GND. The internal pull-ups are used. GPIO 34-39 have no pull-ups, avoid them. |
| Lights in the buttons (optional) | White 5 V LED modules, LED plus on +5 V, LED minus to **GPIO 32 / 33 / 14**; the pin sinks the current, no transistor. Off by default. |
| 5 V power supply | See *Power* below. |

[docs/wiring.md](docs/wiring.md) has the wiring diagram for the minimal
setup (ESP32 box at the strip, buttons over an ethernet cable) with pins,
cable types and lengths. [docs/carrier.md](docs/carrier.md) is a perfboard
layout for the same setup with a removable ESP32, screw terminals, an RJ45
socket for the button cable, a level shifter, and a diode so a USB cable
alone can never power the strip.

**Never plug a USB cable into the ESP32 while the strip's 5 V is wired to the
devkit's 5V/VIN pin**, whether the supply is switched on or not. The PC's USB
port then feeds the strip through the devkit's own small protection diode or
fuse, which overheats: it got 84 °C hot with the supply off, and started to
smoke with the supply on (the supply sags under load, so the PC still
delivers part of the strip current). Flash the ESP32 before wiring it up,
disconnect the devkit's 5 V wire first, use the firmware upload on the
settings page, or use the [carrier board](docs/carrier.md), whose diode makes
USB safe.

The player sits at the start of the strip (LED 0, nearest the data input).
If your strip is the other way around, turn on *Player sits at the far end* in
menuconfig.

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

The settings page shows the current estimate live.

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

After the first flash over USB, later versions can be uploaded from the
settings page (*Firmware*): pick `.pio/build/wemos-esp32/firmware.bin` and
the ESP32 writes it to the second app partition and reboots into it.

### Defaults (`pio run -t menuconfig` or `idf.py menuconfig` → *Mix 'n Smash*)

Everything in menuconfig is a **default**. Once the settings page has saved
something, those values win; *Terug naar standaard* on that page, or holding
the red and blue button while powering on, brings the defaults back.

| Setting | Default |
|---|---|
| LED strip → Number of LEDs / Data GPIO | 300 / 16 |
| LED strip → Player sits at the far end | off (menuconfig only) |
| LED strip → Strip expects RGB instead of GRB | off |
| LED strip → Brightness / Current limit | 80 / 1500 mA |
| Buttons → Red / yellow / blue GPIO | 25 / 26 / 27 |
| Buttons → Buttons connect to 3.3 V instead of GND | off |
| Buttons → Lights in the buttons | off; GPIO 32 / 33 / 14 when on |
| Buttons → Lights are switched by a transistor | off (the GPIO sinks the LED current) |
| Game → A dot hit by the wrong colour keeps coming | off (stricter variant) |
| Game → Frame period | 16 ms (menuconfig only) |
| Wi-Fi → Network to join / password | empty: start an access point |
| Wi-Fi → Access point name / password | `mix-n-smash`, open |
| Wi-Fi → Hostname | `mix-n-smash` |

## Web pages

With no Wi-Fi network configured the ESP32 starts an open access point called
`mix-n-smash`; connect to it and open <http://192.168.4.1>. With a network
configured it joins that network and announces itself as
`http://mix-n-smash.local` (the IP is also printed on the serial monitor and
shown on the settings page). If joining fails six times it falls back to the
access point.

The live view goes over a WebSocket: the ESP32 pushes one status message
every 100 ms to every open page, so a dozen phones can watch at once. Pages
fall back to polling when the WebSocket is not available.

| Page | What |
|---|---|
| `/` **Kijken** | The strip live, level, lives, score and the top-10. Nothing to press. |
| `/play` **Spelen** | Play from a phone. Only there when a player password is set (settings page). After the password: give a name, and you either play right away or wait in the queue. The buttons on the page (and the arrow keys) work for the player whose turn it is; while someone plays from a phone, the buttons on the bar are off. *Ik stop* gives the turn up. |
| `/practice` **Oefenen** | The game in the browser, on a simulated strip, with its own scores and colour panel. Also what `web/index.html` shows when opened from disk. |
| `/settings` **Instellingen** | Protected by the settings password. The first visit asks you to choose one. |

### Players and the queue

- One player at a time. Others who give a name wait in the queue (ten at
  most) and see their position. A name that is already in use is refused with
  the offer to continue as `Name (2)`.
- *Beurtwissel na* (settings) hands the turn to the next in line after 1, 2 or
  3 games; the previous player goes to the back of the queue. *Uit* keeps the
  turn until the player stops.
- On the settings page the queue has *Maak actief* (now) or *Volgende* (from
  the next game, while a game runs), *Verwijder*, and *Spel afbreken*.
- A phone that goes quiet for 30 seconds (page closed, screen off) loses its
  place; a current player loses the turn after 30 quiet seconds when no game
  is running.
- The current player's name goes into the high scores. Games played with the
  physical buttons are recorded as *Speler*.

### Settings page

Groups: Wi-Fi (access point name and password, home network and password,
hostname), Strip (LED count, data GPIO, brightness, current limit, RGB/GRB),
Game (dots in level 1, extra dots per level, speed percentage, games per turn,
stricter wrong-hit rule), Buttons (GPIOs, to GND or 3.3 V) and Lights
(on/off, GPIOs, transistor or not). Brightness, current limit and the game
rules apply at once; the others are marked *herstart nodig* and a banner
offers the restart. Also there: the queue, both passwords (the player
password turns phone play on; *Uitzetten* turns it off), the colour panel
(RGB value of every colour, with the colour chart on the strip while it is
open), the high score list with *Wis*, a firmware upload, *Terug naar
standaard* and *Herstarten*.

Everything runs over plain HTTP on your local network, so the passwords
travel unencrypted; fine for a game at home. A wrong password blocks logins
for two seconds, which makes guessing pointless.

**Forgot the settings password?** Hold the **red and blue** buttons while
powering on. A red bar grows over three seconds; keep holding and the
settings, passwords and colours are reset to the menuconfig defaults (the
strip flashes white three times) and the ESP32 restarts. High scores stay.

### API

Sessions are cookies (`mns_admin`, `mns_player`) set by `/api/login`.

| Method | Path | Who | |
|---|---|---|---|
| GET | `/api/state` | all | state, level, lives, score, player, best, flags, `leds` as hex (same message as the WebSocket at `/ws`) |
| GET | `/api/highscores` | all | top 10 |
| GET | `/api/auth` | all | which passwords exist, which roles this session has |
| POST | `/api/login` | all | `{"role": "admin"\|"player", "password": "..."}` |
| POST | `/api/logout` | all | |
| POST | `/api/password` | admin (first admin password: all) | `{"role": ..., "password": "..."}`, empty removes the player password |
| GET | `/api/queue` | player, admin | current player, queue, your position; doubles as the heartbeat |
| POST | `/api/queue` | player | `{"name": "Bart", "accept_suffix": false}`; 409 with a `suggestion` when the name is in use |
| DELETE | `/api/queue` | player | leave |
| POST | `/api/queue/admin` | admin | `{"action": "activate"\|"remove", "index": n}` (0 = current player) |
| POST | `/api/press` | the current player | `{"color": "red"}` (or `yellow`, `blue`) |
| POST | `/api/game/abort` | admin | stop the running game |
| GET / POST | `/api/settings` | admin | all settings as flat keys; POST takes any subset, answers with `restart_needed` |
| POST | `/api/settings/reset` | admin | back to the defaults |
| POST | `/api/restart` | admin | |
| GET / POST | `/api/palette` | admin | colours as `"rrggbb"` per name; POST any subset |
| POST | `/api/palette/save`, `/api/palette/reset` | admin | |
| POST | `/api/preview` | admin | `{"on": true}` shows the colour chart for another 5 s |
| DELETE | `/api/highscores` | admin | clear the table |
| POST | `/api/ota` | admin | raw `firmware.bin` as the body |

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

The *Dots* column is the default (8 in level 1, +2 per level); the settings
page can change both numbers, and *Snelheid* scales every crossing time
(50 % = everything takes twice as long). The rest of the table is `LEVELS` in
[`game/game.c`](game/game.c) and [`web/game.js`](web/game.js); keep the two
identical (the trace test fails otherwise).

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
web/         index.html, game.js   the four web views and the browser version
firmware/    PlatformIO + ESP-IDF project (main/ has the sources)
docs/        wiring.md, wiring.svg   how to connect everything
             carrier.md, carrier-*pin.svg   perfboard layout with a removable ESP32
tests/       unit tests, trace comparison, Makefile
tools/       build_web.py   bundles web/ into firmware/main/web_page.h
             carrier_layout.py   draws and checks the carrier board layout
```
