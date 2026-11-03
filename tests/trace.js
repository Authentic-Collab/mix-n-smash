// Prints the same trace as trace_main.c, from the JavaScript port.
// `make trace` compares the two outputs byte for byte.
'use strict';
const G = require('../web/game.js');

const CONFIGS = [
  { n_leds: 60, seed: 1, keeps_dot: false, ticks: 6000 },
  { n_leds: 300, seed: 12345, keeps_dot: false, ticks: 6000 },
  { n_leds: 300, seed: 777, keeps_dot: true, ticks: 4000 },
  { n_leds: 16, seed: 99, keeps_dot: false, ticks: 2000 },
];

function lowestBit(mask) { return (mask & 1) ? 1 : (mask & 2) ? 2 : 4; }

// Mirrors tests/autoplay.h.
function autoplayStep(g, tick, presses) {
  if ((g.state === G.STATE.ATTRACT ||
       (g.state === G.STATE.GAME_OVER && g.state_ms >= 800)) && tick % 50 === 0) {
    G.press(g, G.RED);
    return;
  }
  if (g.state !== G.STATE.PLAYING || tick % 7 !== 0) return;

  let nearest = null;
  for (let i = 0; i < G.MAX_DOTS; i++) {
    const d = g.dots[i];
    if (!d.active) continue;
    if (nearest === null || d.pos < nearest.pos) nearest = d;
  }
  if (nearest === null || nearest.pos >= Math.trunc(g.n_leds * 256 * 3 / 4)) return;

  let color = lowestBit(nearest.mask);
  presses.n++;
  if (presses.n % 11 === 0 && nearest.mask !== G.WHITE) {
    if (!(nearest.mask & G.RED)) color = G.RED;
    else if (!(nearest.mask & G.YELLOW)) color = G.YELLOW;
    else color = G.BLUE;
  }
  G.press(g, color);
}

function fnv1a(bytes, n) {
  let h = 2166136261;
  for (let i = 0; i < n * 3; i++) h = Math.imul(h ^ bytes[i], 16777619) >>> 0;
  return h;
}

const lines = [];
CONFIGS.forEach((cfg, c) => {
  const g = G.init(cfg.n_leds, cfg.seed, { wrong_hit_keeps_dot: cfg.keeps_dot });
  const leds = new Uint8Array(G.MAX_LEDS * 3);
  const presses = { n: 0 };
  for (let tick = 0; tick < cfg.ticks; tick++) {
    autoplayStep(g, tick, presses);
    G.tick(g, 16);
    const ev = G.takeEvents(g);
    G.render(g, leds);
    lines.push(`${c} ${tick} ${G.stateName(g.state)} ${g.level} ${g.lives} ${g.score} ${ev} ` +
               fnv1a(leds, g.n_leds).toString(16).padStart(8, '0'));
  }
});
process.stdout.write(lines.join('\n') + '\n');
