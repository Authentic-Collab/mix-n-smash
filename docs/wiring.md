# Wiring (minimal setup)

The ESP32 sits in a small box next to the start of the LED strip, with the
5 V supply (or a power bank). The three buttons are in a separate bar,
connected over an ethernet cable. No resistors, capacitors or level shifter,
and no lights in the buttons: this is the simplest version that works.

![Wiring diagram](wiring.svg)

## Connections

| From | To | Wire | Notes |
|---|---|---|---|
| Supply +5 V | Strip +5 V pad at LED 0 **and** ESP32 `VIN` (5V pin) | 0.75 mm² or thicker, short | Not through the ESP32's USB port |
| Supply GND | Strip GND pad at LED 0 **and** ESP32 `GND` | 0.75 mm² or thicker, short | ESP32 and strip must share GND |
| ESP32 `GPIO 16` | Strip `DIN` at LED 0 | any thin wire, **30 cm at most** | 3.3 V straight into the strip; see note 3 |
| ESP32 `GPIO 25` | Red button | Cat5 white-orange | Button's other side to GND |
| ESP32 `GPIO 26` | Yellow button | Cat5 white-green | |
| ESP32 `GPIO 27` | Blue button | Cat5 white-brown | |
| ESP32 `GND` | Buttons' common | Cat5 blue | |

The other four conductors of the ethernet cable stay unused for now
(orange, green, brown, white-blue). They are reserved for lights in the
buttons later: one for +5 V and three LED returns, switched by transistors
at the ESP32 side.

## Cables and lengths

| Cable | Length | What |
|---|---|---|
| Ethernet, Cat5e or Cat6, solid or stranded | 5 m (up to about 20 m is fine) | Buttons: slow DC signals, no special care needed |
| Data wire ESP32 → strip | 30 cm at most | 800 kHz signal at 3.3 V; keep it short, do not run it through the ethernet cable in this setup |
| Power wires supply → strip and → ESP32 | as short as practical | 0.75 mm² or thicker; the strip draws up to 1 A in this configuration |
| Strip itself | 300 LEDs, 5 m at 60/m or 3 m at 100/m | Feed at LED 0 (player side) |

## Notes

1. **Power the strip straight from the supply**, not through the ESP32
   board. Its USB connector, diode and traces are not made for the strip's
   current. The ESP32 gets 5 V on its `VIN`/`5V` pin from the same supply.
2. **Common ground.** The ESP32 and the strip share GND through the supply;
   without it the data signal has no reference and the strip shows garbage.
3. **Data without a level shifter.** The ESP32 outputs 3.3 V, the strip
   formally wants 3.5 V. In practice a short wire into a WS2812B strip works
   almost always. If the first LEDs flicker or the strip stays dark, add a
   74AHCT125 (or 74HCT245) between `GPIO 16` and `DIN`; that is the only
   extra part this setup might need.
4. **Buttons without resistors.** The firmware enables the ESP32's internal
   pull-ups (about 45 kΩ); a button simply shorts its wire to GND. The
   software debounce ignores anything shorter than 16 ms. If you ever see
   phantom presses on a long cable, add 10 kΩ pull-ups to 3.3 V at the
   ESP32 side.
5. **One feed is enough** at the default brightness (80) with the current
   limit at 1000 mA: nothing in the game lights more than a few dozen LEDs
   at once, and the heavy effects sit next to the feed. Above brightness
   100, also feed the far end with 2 × 1.5 mm².
6. **Power bank.** A bank with a 2 A port is fine: typical draw is about
   0.6 A, peaks around 1.2 A. Cut a short, thick USB cable and solder it to
   the rails; thin USB cables lose 0.3 to 0.5 V at 1 A. Set
   *Current limit* to 1000 mA in menuconfig.
7. **Pins to avoid** for the buttons: GPIO 34 to 39 have no internal
   pull-ups, GPIO 0, 2, 12 and 15 are strapping pins. GPIO 25, 26 and 27 are
   safe on every ESP32 board.
8. **Orientation.** LED 0 (the data input) is next to the player and shows
   the lives; dots come from the far end. If your strip's input ends up at
   the far end, turn on *Player sits at the far end* in menuconfig instead of
   rewiring.

## Firmware settings for this build

`pio run -t menuconfig` → *Mix 'n Smash*:

| Setting | Value |
|---|---|
| LED strip → Number of LEDs | 300 |
| LED strip → Data GPIO | 16 |
| LED strip → Brightness | 80 (default) |
| LED strip → Current limit | 1000 mA (power bank) |
| Buttons → Red / yellow / blue GPIO | 25 / 26 / 27 (default) |
| Buttons → Buttons connect to 3.3 V instead of GND | off (default) |

## Later: lights in the buttons

Use the four spare conductors: white-blue for +5 V to the button bar, and
orange, green, brown as the LED returns. Do not drive 5 V LED modules from a
GPIO directly; switch each return to GND with a 2N7000 MOSFET or one ULN2003
in the ESP32 box, and the firmware can then light, blink or dim the buttons.
