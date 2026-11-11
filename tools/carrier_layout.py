#!/usr/bin/env python3
"""
Perfboard layout of the Mix 'n Smash carrier board, as SVG, with a check.

The ESP32 devkit plugs into two female headers; the board adds a Schottky
diode in its 5 V feed (so a USB cable alone can never power the strip), a
74AHCT125 level shifter for the strip data, a bulk capacitor, screw
terminals for the supply and the strip, and an RJ45 breakout for the cable
to the button bar.

Two variants, because the pin order differs between the common boards:
  30  ESP32 DevKit V1 (DOIT), 15 pins per side
  38  ESP32-DevKitC, 19 pins per side

    tools/carrier_layout.py            write docs/carrier-30pin.svg and -38pin.svg
    tools/carrier_layout.py --check    only run the connectivity check
    tools/carrier_layout.py --table    print the wiring tables (Markdown)

Coordinates: x = column 0..23 (letters A..X), y = row 0..29 (numbers 1..30),
seen from the component side. The check builds the connectivity from the
bare wires, solder bridges and insulated wires, and fails when a net is not
connected in one piece, when two nets touch, or when parts overlap.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
COLS, ROWS = 24, 30

H1_Y = 11      # header with the signals (the devkit's "left" side)
H2_Y = 2       # header on the other side; row 1 (y = 1) on 1-inch wide boards
USB_X = 22     # the pin at the USB end of both headers sits in this column


def hole_name(x, y):
    return f"{chr(ord('A') + x)}{y + 1}"


# Pins from the antenna end to the USB end: (label, role).
# role: GPIO number, '5V', 'GND', '3V3' or None.
VARIANTS = {
    '30': {
        'title': 'ESP32 DevKit V1 (30 pins, DOIT)',
        'h1': [('EN', None), ('VP', 36), ('VN', 39), ('D34', 34), ('D35', 35), ('D32', 32), ('D33', 33),
               ('D25', 25), ('D26', 26), ('D27', 27), ('D14', 14), ('D12', 12), ('D13', 13), ('GND', 'GND'),
               ('VIN', '5V')],
        'h2': [('D23', 23), ('D22', 22), ('TX0', 1), ('RX0', 3), ('D21', 21), ('D19', 19), ('D18', 18),
               ('D5', 5), ('TX2', 17), ('RX2', 16), ('D4', 4), ('D2', 2), ('D15', 15), ('GND', 'GND'),
               ('3V3', '3V3')],
        'overhang': 3.1,     # board edge beyond the outer pins, in holes
    },
    '38': {
        'title': 'ESP32-DevKitC (38 pins)',
        'h1': [('3V3', '3V3'), ('EN', None), ('VP', 36), ('VN', 39), ('IO34', 34), ('IO35', 35),
               ('IO32', 32), ('IO33', 33), ('IO25', 25), ('IO26', 26), ('IO27', 27), ('IO14', 14),
               ('IO12', 12), ('GND', 'GND'), ('IO13', 13), ('SD2', 9), ('SD3', 10), ('CMD', 11),
               ('5V', '5V')],
        'h2': [('GND', 'GND'), ('IO23', 23), ('IO22', 22), ('TX', 1), ('RX', 3), ('IO21', 21),
               ('GND', 'GND'), ('IO19', 19), ('IO18', 18), ('IO5', 5), ('IO17', 17), ('IO16', 16),
               ('IO4', 4), ('IO0', 0), ('IO2', 2), ('IO15', 15), ('SD1', 8), ('SD0', 7), ('CLK', 6)],
        'overhang': 1.7,
    },
}

# What each used ESP32 pin is for.
ROLE_NET = {16: 'DATA', 25: 'BTN_R', 32: 'LIGHT_R', 26: 'BTN_Y', 33: 'LIGHT_Y', 27: 'BTN_B',
            14: 'LIGHT_B', '5V': 'VIN_ESP'}

NET_COLOR = {
    'V5': '#d62828', 'GND': '#1d3557', 'VIN_ESP': '#e07a00', 'U1_A': '#7a7f8a', 'U1_Y': '#7a7f8a',
    'DATA': '#1a9c3c', 'DIN': '#1a9c3c', 'BTN_R': '#e63946', 'BTN_Y': '#d4a106', 'BTN_B': '#2f6bff',
    'LIGHT_R': '#e63946', 'LIGHT_Y': '#d4a106', 'LIGHT_B': '#2f6bff', 'GND_ESP': '#1d3557',
}

# ---- Parts (component side) --------------------------------------------------

# U1 74AHCT125 in a 14-pin socket, notch to the left: pins 1-7 on the lower
# row from left to right, 8-14 on the upper row from right to left.
U1_X0, U1_TOP, U1_BOT = 9, 17, 20
U1_NETS = {1: 'GND', 2: 'U1_A', 3: 'U1_Y', 4: 'GND', 5: 'GND', 6: None, 7: 'GND',
           8: None, 9: 'V5', 10: 'V5', 11: None, 12: 'V5', 13: 'V5', 14: 'V5'}
U1_LABELS = {1: '1OE', 2: '1A', 3: '1Y', 4: '2OE', 5: '2A', 6: '2Y', 7: 'GND',
             8: '3Y', 9: '3A', 10: '3OE', 11: '4Y', 12: '4A', 13: '4OE', 14: 'VCC'}


def u1_pin_pos(n):
    if n <= 7:
        return (U1_X0 + n - 1, U1_BOT)
    return (U1_X0 + 14 - n, U1_TOP)


J1_PINS = [  # 5.08 mm screw terminals on the right edge, wires enter from the right
    ((23, 16), 'V5', '+5V supply'),
    ((23, 18), 'V5', '+5V strip'),
    ((23, 20), 'DIN', 'DIN strip'),
    ((23, 22), 'GND', 'GND strip'),
    ((23, 24), 'GND', 'GND supply'),
]

J2_Y = 24    # RJ45 breakout, 8 pins in a row, socket towards the bottom edge
J2_PINS = [  # (x, net, function), RJ45 pins 1..8 = T568B colours
    (0, 'BTN_R', 'red button'), (1, 'LIGHT_R', 'red light'), (2, 'BTN_Y', 'yellow button'),
    (3, 'GND', 'GND'), (4, 'V5', '+5V'), (5, 'LIGHT_Y', 'yellow light'),
    (6, 'BTN_B', 'blue button'), (7, 'LIGHT_B', 'blue light'),
]
T568B = ['white-orange', 'orange', 'white-green', 'blue', 'white-blue', 'green', 'white-brown', 'brown']

TWO_PIN = [  # ref, value, (x, y) a, net a, (x, y) b, net b, kind
    ('C2', '100 nF', (8, 17), 'V5', (8, 20), 'GND', 'ceramic'),
    ('R3', '100 kΩ', (9, 21), 'U1_A', (9, 25), 'GND', 'resistor'),
    ('R2', '1 kΩ', (10, 21), 'U1_A', (10, 25), 'DATA', 'resistor'),
    ('R1', '330 Ω', (11, 21), 'U1_Y', (11, 25), 'DIN', 'resistor'),
    ('D1', '1N5819', (18, 13), 'V5', (22, 13), 'VIN_ESP', 'diode'),       # a = anode, b = cathode
    ('C1', '1000 µF', (20, 19), 'V5', (20, 21), 'GND', 'elco'),          # a = +
]

# ---- Bare wires and solder bridges (solder side) -----------------------------
# Each path is a list of corner holes; consecutive corners share a row or column.
BARE = [
    # +5V: supply terminal to strip terminal (strip current: thick wire), rail along row 15
    ('V5', [(23, 14), (23, 18)], 'heavy'),
    ('V5', [(9, 14), (23, 14)], 'rail'),
    ('V5', [(23, 18), (20, 18), (20, 19)], 'wire'),          # C1 +
    ('V5', [(18, 13), (18, 14)], 'bridge'),                   # D1 anode
    ('V5', [(9, 14), (9, 17)], 'wire'),                       # U1 VCC
    ('V5', [(9, 17), (11, 17)], 'bridge'),                    # pins 14-13-12
    ('V5', [(11, 17), (11, 16), (13, 16), (13, 17)], 'wire'),  # over pin 11
    ('V5', [(13, 17), (14, 17)], 'bridge'),                   # pins 10-9
    ('V5', [(8, 17), (9, 17)], 'bridge'),                     # C2
    # GND: strip terminal to supply terminal (strip current), rail along row 28
    ('GND', [(23, 22), (23, 24)], 'heavy'),
    ('GND', [(23, 24), (23, 27)], 'wire'),
    ('GND', [(23, 27), (3, 27)], 'rail'),
    ('GND', [(23, 22), (20, 22), (20, 21)], 'wire'),          # C1 -
    ('GND', [(12, 20), (13, 20)], 'bridge'),                  # pins 4-5
    ('GND', [(13, 20), (13, 21), (15, 21), (15, 20)], 'wire'),  # under pin 6 to pin 7
    ('GND', [(15, 21), (15, 27)], 'wire'),
    ('GND', [(8, 20), (9, 20)], 'bridge'),                    # C2 - and pin 1
    ('GND', [(8, 20), (8, 27)], 'wire'),
    ('GND', [(9, 25), (9, 27)], 'wire'),                      # R3
    ('GND', [(3, 24), (3, 27)], 'wire'),                      # RJ45 pin 4
    # ESP32 5V through D1
    ('VIN_ESP', [(22, 13), (22, 11)], 'wire'),
    # level shifter input and output
    ('U1_A', [(10, 20), (10, 21)], 'bridge'),
    ('U1_A', [(10, 21), (9, 21)], 'bridge'),
    ('U1_Y', [(11, 20), (11, 21)], 'bridge'),
]


def insulated(v):
    """Insulated wires for a variant: (net, from, to, note)."""
    h1, h2 = header_positions(v)
    gnd = next(p for p, (lab, role) in h1.items() if role == 'GND')
    data = next(p for p, (lab, role) in h2.items() if role == 16)
    wires = [
        ('DATA', data, (10, 25), 'GPIO 16 to R2'),
        ('GND', gnd, (21, 22), 'ESP32 GND'),
        ('V5', (4, J2_Y), (10, 14), '+5V for the button lights'),
        ('DIN', (11, 25), (23, 20), 'strip data to the terminal'),
    ]
    for x, net, func in J2_PINS:
        if net in ('GND', 'V5'):
            continue
        gpio = next(g for g, n in ROLE_NET.items() if n == net)
        target = next(p for p, (lab, role) in h1.items() if role == gpio)
        wires.append((net, (x, J2_Y), target, f'{func}, GPIO {gpio}'))
    return wires


def header_positions(v):
    spec = VARIANTS[v]
    n = len(spec['h1'])
    first = USB_X - n + 1
    h1 = {(first + i, H1_Y): pin for i, pin in enumerate(spec['h1'])}
    h2 = {(first + i, H2_Y): pin for i, pin in enumerate(spec['h2'])}
    return h1, h2


# ---- Check ---------------------------------------------------------------------

def expand(path):
    holes = []
    for (x0, y0), (x1, y1) in zip(path, path[1:]):
        if x0 != x1 and y0 != y1:
            raise ValueError(f'diagonal segment {path}')
        dx = (x1 > x0) - (x1 < x0)
        dy = (y1 > y0) - (y1 < y0)
        x, y = x0, y0
        while (x, y) != (x1, y1):
            holes.append((x, y))
            x, y = x + dx, y + dy
    holes.append(path[-1])
    return holes


def pads(v):
    """Every pad that gets soldered: {(x, y): (net or None, description)}."""
    out = {}

    def add(pos, net, desc):
        if pos in out:
            raise ValueError(f'two parts on hole {hole_name(*pos)}: {out[pos][1]} and {desc}')
        x, y = pos
        if not (0 <= x < COLS and 0 <= y < ROWS):
            raise ValueError(f'{desc} is off the board')
        out[pos] = (net, desc)

    h1, h2 = header_positions(v)
    for pos, (lab, role) in h1.items():
        add(pos, 'GND' if role == 'GND' else ROLE_NET.get(role), f'ESP32 {lab}')
    for pos, (lab, role) in h2.items():
        add(pos, ROLE_NET.get(role) if role == 16 else None, f'ESP32 {lab} (other side)')
    for n in range(1, 15):
        add(u1_pin_pos(n), U1_NETS[n], f'U1 pin {n} {U1_LABELS[n]}')
    for pos, net, lab in J1_PINS:
        add(pos, net, f'J1 {lab}')
    for x, net, func in J2_PINS:
        add((x, J2_Y), net, f'J2 pin {x + 1} {func}')
    for ref, val, a, na, b, nb, kind in TWO_PIN:
        add(a, na, f'{ref} {"+" if kind == "elco" else "anode" if kind == "diode" else "a"}')
        add(b, nb, f'{ref} {"-" if kind == "elco" else "cathode" if kind == "diode" else "b"}')
    return out


def check(v):
    errors = []
    try:
        pad = pads(v)
    except ValueError as e:
        return [str(e)]

    parent = {}

    def find(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b):
        parent[find(a)] = find(b)

    hole_net = {}           # holes touched by bare copper and the net they should carry
    for net, path, style in BARE:
        try:
            holes = expand(path)
        except ValueError as e:
            errors.append(str(e))
            continue
        for h in holes:
            x, y = h
            if not (0 <= x < COLS and 0 <= y < ROWS):
                errors.append(f'bare {net} wire leaves the board at {h}')
            if h in pad and pad[h][0] != net:
                errors.append(f'bare {net} wire touches {pad[h][1]} ({hole_name(*h)})')
            if hole_net.get(h, net) != net:
                errors.append(f'bare {net} and {hole_net[h]} wires meet at {hole_name(*h)}')
            hole_net[h] = net
        for a, b in zip(holes, holes[1:]):
            union(a, b)
    for net, a, b, note in insulated(v):
        for h in (a, b):
            got = pad.get(h, (hole_net.get(h), ''))[0] if h in pad else hole_net.get(h)
            if got != net:
                errors.append(f'insulated {net} wire ({note}) ends on {hole_name(*h)}, which is {got}')
        union(a, b)

    # every pad of a net in one group, no group with two nets
    groups = {}
    for h, (net, desc) in pad.items():
        if net is None:
            continue
        groups.setdefault(net, set()).add(find(h))
    for h, net in hole_net.items():
        groups.setdefault(net, set()).add(find(h))
    for net, roots in groups.items():
        if len(roots) > 1:
            parts = sorted(desc for h, (n, desc) in pad.items() if n == net)
            errors.append(f'net {net} is in {len(roots)} pieces: {", ".join(parts)}')
    owner = {}
    for net, roots in groups.items():
        for r in roots:
            if r in owner and owner[r] != net:
                errors.append(f'nets {owner[r]} and {net} are shorted')
            owner[r] = net
    for h, (net, desc) in pad.items():
        if net is None and find(h) in owner:
            errors.append(f'{desc} ({hole_name(*h)}) should stay free but touches {owner[find(h)]}')

    # bodies on the component side must not overlap (rough rectangles, in holes)
    bodies = [(ref, box) for ref, box in body_boxes(v)]
    for i, (ra, a) in enumerate(bodies):
        for rb, b in bodies[i + 1:]:
            if a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]:
                errors.append(f'{ra} and {rb} overlap')
    return errors


def body_boxes(v):
    h1, _ = header_positions(v)
    first = min(x for x, _ in h1)
    oh = VARIANTS[v]['overhang']
    yield 'ESP32', (first - oh, H2_Y - 1.1, USB_X + oh, H1_Y + 1.1)
    yield 'U1', (U1_X0 - 0.45, U1_TOP - 0.45, U1_X0 + 6.45, U1_BOT + 0.45)
    yield 'J1', (22.45, 15.0, 24.4, 25.0)
    yield 'J2', (-0.45, J2_Y + 0.3, 7.45, 30.5)
    yield 'C1', (18.45, 18.45, 21.55, 21.55)
    yield 'D1', (18.6, 12.6, 21.4, 13.4)
    for ref, val, a, na, b, nb, kind in TWO_PIN:
        if kind == 'resistor':
            yield ref, (a[0] - 0.42, a[1] + 0.55, a[0] + 0.42, b[1] - 0.55)
        if kind == 'ceramic':
            yield ref, (a[0] - 0.3, a[1] + 0.9, a[0] + 0.3, b[1] - 0.9)


# ---- SVG -------------------------------------------------------------------------

S = 22.0          # pixels per hole
PAD = 1.4         # board margin around the outer holes, in holes


def esc(t):
    return str(t).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


class Panel:
    def __init__(self, ox, oy, mirror):
        self.ox, self.oy, self.mirror = ox, oy, mirror

    def px(self, x, y):
        if self.mirror:
            x = COLS - 1 - x
        return self.ox + (x + PAD) * S, self.oy + (y + PAD) * S


def svg_variant(v):
    spec = VARIANTS[v]
    h1, h2 = header_positions(v)
    pad = pads(v)
    wires = insulated(v)
    bw = (COLS - 1 + 2 * PAD) * S
    bh = (ROWS - 1 + 2 * PAD) * S
    top = Panel(40, 120, False)
    bot = Panel(40 + bw + 200, 120, True)
    width = int(bot.ox + bw + 40)
    height = int(120 + bh + 460)
    o = []
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
             f'viewBox="0 0 {width} {height}" font-family="system-ui, -apple-system, Segoe UI, Roboto, sans-serif">')
    o.append('<style>.t{fill:#222}.m{fill:#555;font-size:11px}.s{font-size:8.5px;fill:#444}'
             '.lab{font-size:10px;fill:#222}.h{font-size:15px;font-weight:700;fill:#222}</style>')
    o.append(f'<rect width="{width}" height="{height}" fill="#ffffff"/>')
    o.append(f'<text x="40" y="38" font-size="21" font-weight="700" class="t">Mix \'n Smash carrier board: '
             f'{esc(spec["title"])}</text>')
    o.append('<text x="40" y="60" class="m">Perfboard 7 × 9 cm, 24 × 30 holes. Columns A–X, rows 1–30. '
             'Red = +5 V, dark blue = GND, orange = ESP32 5 V after the diode, grey = other bare links. '
             'Thin coloured lines = insulated wire.</text>')
    o.append('<text x="40" y="78" class="m">Thick bare lines: tinned copper wire on the solder side '
             '(the two short links in column X carry the strip current: 0.8–1 mm wire). '
             'Short grey bars: solder bridges between neighbouring holes.</text>')

    for panel, title in ((top, 'Component side (from above)'), (bot, 'Solder side (from below, mirrored)')):
        x0, y0 = panel.ox, panel.oy
        o.append(f'<text x="{x0}" y="{y0 - 26}" class="h">{title}</text>')
        o.append(f'<rect x="{x0}" y="{y0}" width="{bw}" height="{bh}" rx="10" fill="#efe2c2" stroke="#b9a77c"/>')
        for x in range(COLS):
            cx, _ = panel.px(x, 0)
            o.append(f'<text x="{cx}" y="{y0 - 6}" class="s" text-anchor="middle">{chr(65 + x)}</text>')
        for y in range(ROWS):
            _, cy = panel.px(0 if not panel.mirror else COLS - 1, y)
            o.append(f'<text x="{x0 - 6}" y="{cy + 3}" class="s" text-anchor="end">{y + 1}</text>')
        for x in range(COLS):
            for y in range(ROWS):
                cx, cy = panel.px(x, y)
                used = (x, y) in pad
                ring = '#c47a2c' if used else '#d6a565'
                o.append(f'<circle cx="{cx}" cy="{cy}" r="{4.6 if used else 4.0}" fill="{ring}"/>'
                         f'<circle cx="{cx}" cy="{cy}" r="1.6" fill="#5a4630"/>')

    def line(panel, a, b, color, width, extra=''):
        ax, ay = panel.px(*a)
        bx, by = panel.px(*b)
        return (f'<line x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="{color}" '
                f'stroke-width="{width}" stroke-linecap="round" {extra}/>')

    def poly(panel, path, color, width, opacity=0.9):
        pts = ' '.join('%.1f,%.1f' % panel.px(*p) for p in path)
        return (f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" '
                f'stroke-linecap="round" stroke-linejoin="round" opacity="{opacity}"/>')

    def bare(panel):
        for net, path, style in BARE:
            color = NET_COLOR.get(net, '#7a7f8a')
            if style == 'bridge':
                o.append(poly(panel, path, '#8d939c', 6, 0.95))
            else:
                w = {'heavy': 8, 'rail': 5.5, 'wire': 4}[style]
                o.append(poly(panel, path, color, w, 0.85))

    def insulated_wires(panel, faint=False):
        for i, (net, a, b, note) in enumerate(wires):
            color = NET_COLOR.get(net, '#444')
            ax, ay = panel.px(*a)
            bx, by = panel.px(*b)
            mx, my = (ax + bx) / 2, (ay + by) / 2
            dx, dy = bx - ax, by - ay
            ln = max(1.0, (dx * dx + dy * dy) ** 0.5)
            bend = 18 + (i % 4) * 6
            cx, cy = mx - dy / ln * bend, my + dx / ln * bend
            dash = ' stroke-dasharray="6 4"' if net.startswith('LIGHT') else ''
            op = 0.35 if faint else 0.95
            o.append(f'<path d="M{ax:.1f},{ay:.1f} Q{cx:.1f},{cy:.1f} {bx:.1f},{by:.1f}" fill="none" '
                     f'stroke="#ffffff" stroke-width="5" opacity="{op}"/>')
            o.append(f'<path d="M{ax:.1f},{ay:.1f} Q{cx:.1f},{cy:.1f} {bx:.1f},{by:.1f}" fill="none" '
                     f'stroke="{color}" stroke-width="2.4"{dash} opacity="{op}"/>')
            for px_, py_ in ((ax, ay), (bx, by)):
                o.append(f'<circle cx="{px_:.1f}" cy="{py_:.1f}" r="3.2" fill="{color}" opacity="{op}"/>')

    # ---------- solder side: wires only, with faint outlines of the parts
    bare(bot)
    insulated_wires(bot)

    # ---------- component side
    bare(top)          # shown through the board, faded below the parts
    o.append(f'<rect x="{top.ox}" y="{top.oy}" width="{bw}" height="{bh}" rx="10" fill="#efe2c2" opacity="0.55"/>')
    insulated_wires(top, faint=True)

    def rect(panel, x0, y0, x1, y1, fill, stroke, extra='', rx=3):
        ax, ay = panel.px(x0, y0)
        bx, by = panel.px(x1, y1)
        lx, rx_ = min(ax, bx), max(ax, bx)
        return (f'<rect x="{lx:.1f}" y="{min(ay, by):.1f}" width="{rx_ - lx:.1f}" height="{abs(by - ay):.1f}" '
                f'rx="{rx}" fill="{fill}" stroke="{stroke}" {extra}/>')

    def text(panel, x, y, s, cls='lab', anchor='middle', extra=''):
        tx, ty = panel.px(x, y)
        return f'<text x="{tx:.1f}" y="{ty:.1f}" class="{cls}" text-anchor="{anchor}" {extra}>{esc(s)}</text>'

    for panel, faint in ((top, False), (bot, True)):
        op = 0.25 if faint else 1.0
        g = f'<g opacity="{op}">'
        o.append(g)
        # ESP32 outline, module and USB
        first = min(x for x, _ in h1)
        oh = spec['overhang']
        o.append(rect(panel, first - oh, H2_Y - 1.1, USB_X + oh, H1_Y + 1.1, '#2b6cb0', '#1a365d',
                      'fill-opacity="0.10" stroke-dasharray="6 4" stroke-width="1.5"', 6))
        o.append(rect(panel, first - oh, H2_Y + 0.9, first - oh + 10, H1_Y - 0.9, '#9aa5b1', '#5d6b78',
                      'fill-opacity="0.25"', 2))
        o.append(text(panel, first - oh + 5, (H1_Y + H2_Y) / 2 + 0.2, 'ESP32 module', 'lab',
                      extra='font-weight="700" fill="#33415c"'))
        o.append(text(panel, first - oh + 5, (H1_Y + H2_Y) / 2 + 1.0, '(antenna end)', 's'))
        o.append(rect(panel, USB_X + oh - 0.6, (H1_Y + H2_Y) / 2 - 1.2, USB_X + oh + 0.5,
                      (H1_Y + H2_Y) / 2 + 1.2, '#a0aec0', '#4a5568', '', 2))
        o.append(text(panel, USB_X + oh - 1.6, (H1_Y + H2_Y) / 2 + 0.3, 'USB', 'lab', extra='font-weight="700"'))
        o.append('</g>')

    # header labels (component side only, bold for the used pins)
    for pos, (lab, role) in list(h1.items()) + list(h2.items()):
        x, y = pos
        used = pad[pos][0] is not None
        dy = -0.62 if y == H1_Y else 0.95
        tx, ty = top.px(x, y + dy)
        o.append(text(top, x, y + dy, lab, 's', 'start' if y == H1_Y else 'end',
                      (f'font-weight="700" fill="{NET_COLOR.get(pad[pos][0], "#222")}"' if used else
                       'fill="#7b8794"') + f' transform="rotate(-90 {tx:.1f} {ty:.1f})" dominant-baseline="middle"'))
    # header strips
    for row in (H1_Y, H2_Y):
        xs = [x for x, y in pad if y == row and pad[(x, y)][1].startswith('ESP32')]
        o.append(rect(top, min(xs) - 0.45, row - 0.45, max(xs) + 0.45, row + 0.45, 'none', '#222',
                      'stroke-width="1.2"', 2))
    o.append(text(top, min(x for x, _ in h1) - 0.7, H1_Y + 0.25, 'H1', 'lab', 'end', 'font-weight="700"'))
    o.append(text(top, min(x for x, _ in h2) - 0.7, H2_Y + 0.25, 'H2', 'lab', 'end', 'font-weight="700"'))

    # U1 with socket
    o.append(rect(top, U1_X0 - 0.45, U1_TOP - 0.45, U1_X0 + 6.45, U1_BOT + 0.45, '#f7f7f7', '#333',
                  'stroke-width="1.3"', 3))
    o.append(rect(top, U1_X0 - 0.1, U1_TOP + 0.35, U1_X0 + 6.1, U1_BOT - 0.35, '#2d3748', '#1a202c', '', 2))
    nx, ny = top.px(U1_X0 - 0.1, (U1_TOP + U1_BOT) / 2)
    o.append(f'<circle cx="{nx:.1f}" cy="{ny:.1f}" r="5" fill="#f7f7f7"/>')
    o.append(text(top, U1_X0 + 3, (U1_TOP + U1_BOT) / 2 + 0.2, '74AHCT125', 'lab',
                  extra='fill="#ffffff" font-weight="700"'))
    px1, py1 = top.px(*u1_pin_pos(1))
    o.append(f'<circle cx="{px1 + 0:.1f}" cy="{py1 - 9:.1f}" r="2.2" fill="#ffffff"/>')
    for n in range(1, 15):
        x, y = u1_pin_pos(n)
        o.append(text(top, x, y + (0.95 if y == U1_BOT else -0.6), str(n), 's'))

    # two-pin parts
    for ref, val, a, na, b, nb, kind in TWO_PIN:
        ax, ay = top.px(*a)
        bx, by = top.px(*b)
        o.append(f'<line x1="{ax:.1f}" y1="{ay:.1f}" x2="{bx:.1f}" y2="{by:.1f}" stroke="#666" stroke-width="1.6"/>')
        if kind == 'resistor':
            o.append(rect(top, a[0] - 0.36, a[1] + 0.7, a[0] + 0.36, b[1] - 0.7, '#e9d8a6', '#7a6a3a', '', 4))
            mx, my = top.px(a[0], (a[1] + b[1]) / 2)
            o.append(f'<text x="{mx:.1f}" y="{my:.1f}" class="s" text-anchor="middle" '
                     f'transform="rotate(-90 {mx:.1f} {my:.1f})" dominant-baseline="middle">{esc(ref)}</text>')
        elif kind == 'ceramic':
            o.append(rect(top, a[0] - 0.3, a[1] + 0.95, a[0] + 0.3, b[1] - 0.95, '#f4a261', '#9c5a1d', '', 5))
        elif kind == 'diode':
            o.append(rect(top, 18.9, 12.68, 21.1, 13.32, '#2d3748', '#1a202c', '', 3))
            o.append(rect(top, 20.65, 12.68, 20.9, 13.32, '#e2e8f0', 'none', '', 0))
        elif kind == 'elco':
            cx, cy = top.px((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)
            o.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{1.55 * S:.1f}" fill="#3b4f8a" stroke="#1f2b4d"/>')
            o.append(f'<path d="M{cx - 1.55 * S:.1f},{cy:.1f} A{1.55 * S:.1f},{1.55 * S:.1f} 0 0 0 '
                     f'{cx + 1.55 * S:.1f},{cy:.1f}" fill="#c9d3ee" opacity="0.9"/>')
            o.append(f'<text x="{cx:.1f}" y="{cy - 6:.1f}" class="lab" text-anchor="middle" fill="#ffffff" '
                     f'font-weight="700">+</text>')
            o.append(f'<text x="{cx:.1f}" y="{cy + 15:.1f}" class="s" text-anchor="middle">−</text>')
    labels = {
        'C2': (6.9, 18.7, 'end'), 'R3': (9, 26.3, 'middle'), 'R2': (10, 26.3, 'middle'),
        'R1': (11.2, 26.3, 'middle'), 'D1': (17.55, 13.25, 'end'), 'C1': (18.2, 22.5, 'end'),
    }
    for ref, val, a, na, b, nb, kind in TWO_PIN:
        lx, ly, anc = labels[ref]
        if ref in ('R3', 'R2', 'R1'):
            continue
        o.append(text(top, lx, ly, f'{ref} {val}', 'lab', anc, 'font-weight="600"'))
    o.append(text(top, 12.3, 26.0, 'R1 330 Ω · R2 1 kΩ · R3 100 kΩ', 's', 'start', 'font-weight="600"'))

    # J1 screw terminals
    o.append(rect(top, 22.45, 15.0, 24.4, 25.0, '#3fa34d', '#24632d', 'fill-opacity="0.85"', 3))
    for (x, y), net, lab in J1_PINS:
        o.append(text(top, 24.85, y + 0.25, lab, 'lab', 'start',
                      f'font-weight="700" fill="{NET_COLOR.get(net, "#222")}"'))
        sx, sy = top.px(23.7, y)
        o.append(f'<circle cx="{sx:.1f}" cy="{sy:.1f}" r="5" fill="#cbd5e0" stroke="#4a5568"/>'
                 f'<line x1="{sx - 3.5:.1f}" y1="{sy:.1f}" x2="{sx + 3.5:.1f}" y2="{sy:.1f}" stroke="#4a5568" stroke-width="1.4"/>')
    o.append(text(top, 23.4, 25.75, 'J1', 'lab', 'middle', 'font-weight="700"'))
    o.append(text(top, 23.4, 26.45, 'wires in →', 's', 'middle'))

    # J2 RJ45 breakout
    o.append(rect(top, -0.45, J2_Y - 0.45, 7.45, 30.4, '#cbd5e0', '#4a5568', 'fill-opacity="0.75"', 4))
    o.append(rect(top, 1.2, 26.6, 5.8, 30.4, '#a0aec0', '#2d3748', '', 2))
    o.append(text(top, 3.5, 28.6, 'RJ45 socket', 'lab', 'middle', 'font-weight="700"'))
    o.append(text(top, 3.5, 29.3, '(opening at the board edge)', 's', 'middle'))
    o.append(text(top, 3.5, 25.95, 'J2  RJ45 breakout', 'lab', 'middle', 'font-weight="700"'))
    for x, net, func in J2_PINS:
        o.append(text(top, x, J2_Y - 0.62, str(x + 1), 's', 'middle',
                      f'font-weight="700" fill="{NET_COLOR.get(net, "#222")}"'))

    # pads on top of everything on the component side
    for (x, y), (net, desc) in pad.items():
        cx, cy = top.px(x, y)
        o.append(f'<circle cx="{cx}" cy="{cy}" r="3.2" fill="#c47a2c" stroke="#5a4630" stroke-width="0.8"/>')

    # solder side labels for orientation
    for pos, (lab, role) in list(h1.items()) + list(h2.items()):
        if pad[pos][0] is None:
            continue
        x, y = pos
        o.append(text(bot, x, y + (-0.62 if y == H1_Y else 0.95), lab, 's', 'middle',
                      f'font-weight="700" fill="{NET_COLOR.get(pad[pos][0], "#222")}"'))
    for (x, y), net, lab in J1_PINS:
        o.append(text(bot, 25.7, y + 0.25, lab, 'lab', 'end',
                      f'font-weight="700" fill="{NET_COLOR.get(net, "#222")}"'))
    for x, net, func in J2_PINS:
        o.append(text(bot, x, J2_Y + 0.95, str(x + 1), 's', 'middle', 'font-weight="700"'))
    for n in (1, 7, 8, 14):
        x, y = u1_pin_pos(n)
        o.append(text(bot, x, y + (0.95 if y == U1_BOT else -0.6), f'U1.{n}', 's', 'middle'))

    # notes
    ny0 = 120 + bh + 34
    notes = [
        'Mount the ESP32 with its USB connector to the right (column W side), as drawn. The other way round fits too, but puts 5 V on the wrong pins: mark the board.',
        'H2 (row 3) only holds the ESP32, except the GPIO 16 pin, which carries the strip data. On a 1-inch wide devkit H2 goes in row 2: solder both headers with the ESP32 plugged in.',
        'D1 (stripe towards the ESP32) feeds the ESP32 from the 5 V supply but blocks the other way: a USB cable alone can no longer power the strip.',
        'R2 limits the current into U1 while the strip is unpowered; R3 keeps the data line low while the ESP32 boots, so the strip does not flicker.',
        'Without U1: put a wire link in the socket from pin 2 to pin 3. R1 and R2 then sit in series in the data line, which is fine.',
        'J2: check with a continuity tester that header pin 1 really goes to RJ45 contact 1. Label the socket "no network": PoE switches put up to 48 V on these pins.',
        'Insulated wires may cross anything. Keep them away from the area under the ESP32 antenna (left end) where you can.',
    ]
    o.append(f'<text x="40" y="{ny0}" class="h">Notes</text>')
    for i, n in enumerate(notes):
        o.append(f'<text x="40" y="{ny0 + 24 + i * 19}" class="m">{i + 1}. {esc(n)}</text>')
    # legend of insulated wires
    ly = ny0 + 24 + len(notes) * 19 + 16
    o.append(f'<text x="40" y="{ly}" class="h">Insulated wires</text>')
    for i, (net, a, b, note) in enumerate(wires):
        col = i % 2
        row = i // 2
        lx = 40 + col * 560
        yy = ly + 22 + row * 19
        dash = ' stroke-dasharray="6 4"' if net.startswith('LIGHT') else ''
        o.append(f'<line x1="{lx}" y1="{yy - 4}" x2="{lx + 26}" y2="{yy - 4}" stroke="{NET_COLOR.get(net, "#444")}" '
                 f'stroke-width="2.6"{dash}/>')
        o.append(f'<text x="{lx + 34}" y="{yy}" class="m">{esc(hole_name(*a))} → {esc(hole_name(*b))}: {esc(note)}</text>')
    o.append('</svg>')
    return '\n'.join(o) + '\n', int(ly + 22 + ((len(wires) + 1) // 2) * 19 + 10)


def write_svgs():
    for v in VARIANTS:
        svg, needed_h = svg_variant(v)
        # trim the canvas to the content
        import re
        svg = re.sub(r'height="\d+" viewBox="0 0 (\d+) \d+"', lambda m: f'height="{needed_h}" viewBox="0 0 {m.group(1)} {needed_h}"', svg, count=1)
        svg = re.sub(r'<rect width="(\d+)" height="\d+" fill="#ffffff"/>', lambda m: f'<rect width="{m.group(1)}" height="{needed_h}" fill="#ffffff"/>', svg, count=1)
        out = ROOT / 'docs' / f'carrier-{v}pin.svg'
        out.write_text(svg, encoding='utf-8')
        print(f'wrote {out.relative_to(ROOT)}')


def tables():
    print('\n#### Parts (both variants)\n')
    print('| Part | Holes | Notes |')
    print('|---|---|---|')
    print(f'| U1 74AHCT125 (socket) | pin 1 at {hole_name(*u1_pin_pos(1))}, pin 14 at {hole_name(*u1_pin_pos(14))} '
          f'| notch to the left (towards column A) |')
    for ref, val, a, na, b, nb, kind in TWO_PIN:
        note = {'diode': 'stripe (cathode) at ' + hole_name(*b), 'elco': '+ at ' + hole_name(*a),
                'resistor': '', 'ceramic': ''}[kind]
        print(f'| {ref} {val} | {hole_name(*a)} – {hole_name(*b)} | {note} |')
    print(f'| J1 screw terminals | {", ".join(hole_name(*p) + " " + lab for p, net, lab in J1_PINS)} | '
          f'wires enter from the right edge |')
    print(f'| J2 RJ45 breakout | pins 1–8 at {hole_name(J2_PINS[0][0], J2_Y)}–{hole_name(J2_PINS[-1][0], J2_Y)} '
          f'| socket opening at the bottom edge |')
    print('\n#### Bare wires and solder bridges (both variants)\n')
    print('| Net | Path | Kind |')
    print('|---|---|---|')
    names = {'V5': '+5V', 'GND': 'GND', 'VIN_ESP': 'ESP32 5V', 'U1_A': 'U1 input', 'U1_Y': 'U1 output'}
    kinds = {'heavy': 'thick wire (strip current)', 'rail': 'rail', 'wire': 'wire', 'bridge': 'solder bridge'}
    for net, path, style in BARE:
        print(f'| {names.get(net, net)} | {" → ".join(hole_name(*p) for p in path)} | {kinds[style]} |')
    for v, spec in VARIANTS.items():
        print(f'\n#### {spec["title"]}\n')
        print('| From | To | What |')
        print('|---|---|---|')
        for net, a, b, note in insulated(v):
            print(f'| {hole_name(*a)} | {hole_name(*b)} | {note} |')
        h1, h2 = header_positions(v)
        first = min(x for x, _ in h1)
        print(f'\nHeaders: H1 in row {H1_Y + 1}, H2 in row {H2_Y + 1}, '
              f'columns {chr(65 + first)}–{chr(65 + USB_X)}.')


def main():
    failed = False
    for v in VARIANTS:
        errs = check(v)
        for e in errs:
            print(f'[{v}-pin] {e}')
        failed |= bool(errs)
    if failed:
        sys.exit(1)
    print('connectivity check passed for both variants')
    if '--check' in sys.argv[1:]:
        return
    if '--table' in sys.argv[1:]:
        tables()
        return
    write_svgs()


if __name__ == '__main__':
    main()
