/*
 * Mix 'n Smash game rules: a statement-by-statement port of game/game.c.
 * Read that file for the rules and the reasoning. Both use the same integer
 * arithmetic and random generator, so for the same seed and inputs they light
 * the same LEDs; tests/trace.js and tests/trace_main.c check that.
 *
 * Works as a plain <script> (window.MixNSmash) and as a CommonJS module.
 */
(function (root, factory) {
  var api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.MixNSmash = api;
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  var ONE = 256;                       /* positions are LED index * 256 */

  var MIN_LEDS = 16, MAX_LEDS = 400, LIVES_LEDS = 3, PLAY_START = 4, MAX_LIVES = 3;
  var MAX_DOTS = 16, MAX_SHOTS = 12, MAX_FLASHES = 8, LEVEL_COUNT = 10;

  var RED = 1, YELLOW = 2, BLUE = 4;
  var ORANGE = RED | YELLOW, PURPLE = RED | BLUE, GREEN = YELLOW | BLUE, WHITE = RED | YELLOW | BLUE;

  var STATE = { ATTRACT: 0, COUNTDOWN: 1, PLAYING: 2, LEVEL_CLEAR: 3, GAME_OVER: 4 };
  var STATE_NAMES = ['attract', 'countdown', 'playing', 'level_clear', 'game_over'];

  var EV = {
    START: 1 << 0, SHOT: 1 << 1, DOT_CLEARED: 1 << 2, DOT_REDUCED: 1 << 3, WRONG_HIT: 1 << 4,
    DOT_ESCAPED: 1 << 5, LIFE_LOST: 1 << 6, LEVEL_CLEAR: 1 << 7, GAME_OVER: 1 << 8
  };

  var COUNTDOWN_FIRST_MS = 3000, COUNTDOWN_NEXT_MS = 2000;
  var LEVEL_CLEAR_MS = 2600, LEVEL_CLEAR_RAINBOW_MS = 1400;
  var GAME_OVER_MS = 6000, GAME_OVER_PULSE_MS = 1600, GAME_OVER_PULSE_PERIOD = 800, GAME_OVER_RESTART_MS = 800;
  var BLINK_MS = 300, SHOT_CROSS_MS = 900, FIRST_SPAWN_MS = 400, DT_MAX_MS = 50;
  var FLASH_CLEAR_MS = 260, FLASH_REDUCE_MS = 180, ZONE_FLASH_WRONG_MS = 350, ZONE_FLASH_ESCAPE_MS = 450;
  var ATTRACT_COMETS = 6, ATTRACT_PERIOD_MS = 1500, COMET_TAIL = 4;
  var ZONE_MAX_LEDS = 20, RAINBOW_MAX_LEDS = 12, SPAWN_SPACING_LEDS = 4, POINTS_PER_COLOR = 10;

  /* dots, cross_ms, mixed%, triple%, max_concurrent, gap_min_ms, gap_max_ms, speed_var% */
  var LEVELS = [
    [ 8, 7000,  0,  0, 1, 1500, 2500,  0],
    [10, 6000, 25,  0, 2, 1300, 2300,  0],
    [12, 5200, 40,  0, 2, 1100, 2000,  0],
    [14, 4600, 50,  0, 3,  950, 1800,  0],
    [16, 4000, 55, 10, 3,  850, 1600, 10],
    [18, 3500, 60, 15, 4,  750, 1400, 15],
    [20, 3100, 65, 20, 4,  650, 1200, 20],
    [22, 2800, 70, 25, 5,  600, 1100, 20],
    [24, 2500, 70, 30, 5,  550, 1000, 25],
    [26, 2200, 75, 35, 6,  500,  900, 25]
  ].map(function (r) {
    return { dots: r[0], cross_ms: r[1], mixed_pct: r[2], triple_pct: r[3],
             max_concurrent: r[4], gap_min_ms: r[5], gap_max_ms: r[6], speed_var_pct: r[7] };
  });

  var WHEEL = [RED, ORANGE, YELLOW, GREEN, BLUE, PURPLE];
  var PRIMARIES = [RED, YELLOW, BLUE];
  var SECONDARIES = [ORANGE, PURPLE, GREEN];

  var COL_OFF = rgb(0, 0, 0);
  var COL_LIFE = rgb(90, 90, 90);
  var COL_COUNTDOWN = rgb(80, 80, 80);
  var COL_LEVEL = rgb(255, 255, 255);
  var COL_FLASH_RED = rgb(255, 0, 0);
  var COL_CLEARED = rgb(255, 255, 255);

  /* ---- Small helpers ---------------------------------------------------- */

  function rgb(r, g, b) { return { r: r, g: g, b: b }; }

  /* C integer division: truncate towards zero. */
  function idiv(a, b) { return Math.trunc(a / b); }

  function lerpI(a, b, k, n) { return a + idiv((b - a) * k, n); }

  function popcount3(m) { return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1); }

  function rngNext(g) {
    var x = g.rng;
    x = (x ^ (x << 13)) >>> 0;
    x = (x ^ (x >>> 17)) >>> 0;
    x = (x ^ (x << 5)) >>> 0;
    g.rng = x;
    return x;
  }

  function rngRange(g, n) { return rngNext(g) % n; }

  function playLen(g) { return g.n_leds - PLAY_START; }

  function dotLen(g) { return g.n_leds >= 200 ? 3 : g.n_leds >= 100 ? 2 : 1; }

  function zoneLen(g) { return Math.min(ZONE_MAX_LEDS, idiv(playLen(g), 3)); }

  function scale(c, num, den) {
    return rgb(idiv(c.r * num, den), idiv(c.g * num, den), idiv(c.b * num, den));
  }

  function colorForMask(mask) {
    switch (mask) {
      case RED:    return rgb(255, 0, 0);
      case YELLOW: return rgb(255, 190, 0);
      case BLUE:   return rgb(0, 0, 255);
      case ORANGE: return rgb(255, 70, 0);
      case PURPLE: return rgb(170, 0, 255);
      case GREEN:  return rgb(0, 255, 0);
      case WHITE:  return rgb(255, 255, 255);
      default:     return COL_OFF;
    }
  }

  function levelParams(level) {
    var idx = level - 1;
    if (idx < 0) idx = 0;
    if (idx >= LEVEL_COUNT) idx = LEVEL_COUNT - 1;
    return LEVELS[idx];
  }

  function stateName(state) { return STATE_NAMES[state] || '?'; }

  function activeDots(g) {
    var n = 0;
    for (var i = 0; i < MAX_DOTS; i++) if (g.dots[i].active) n++;
    return n;
  }

  /* ---- State changes ---------------------------------------------------- */

  function newDot() { return { active: false, mask: 0, colors_at_spawn: 0, pos: 0, speed: 0 }; }
  function newShot() { return { active: false, mask: 0, pos: 0, speed: 0 }; }
  function newFlash() { return { active: false, led: 0, color: COL_OFF, ttl_ms: 0, total_ms: 0 }; }

  function setState(g, state) { g.state = state; g.state_ms = 0; }

  function clearShots(g) { for (var i = 0; i < MAX_SHOTS; i++) g.shots[i] = newShot(); }
  function clearDots(g) { for (var i = 0; i < MAX_DOTS; i++) g.dots[i] = newDot(); }

  function clearField(g) {
    clearDots(g);
    clearShots(g);
    for (var i = 0; i < MAX_FLASHES; i++) g.flashes[i] = newFlash();
    g.zone_flash_ms = 0;
    g.zone_flash_total_ms = 0;
  }

  function enterAttract(g) {
    clearField(g);
    setState(g, STATE.ATTRACT);
  }

  function startCountdown(g, ms) {
    clearField(g);
    g.countdown_ms = ms;
    setState(g, STATE.COUNTDOWN);
  }

  function startLevel(g) {
    var p = levelParams(g.level);
    clearDots(g);
    clearShots(g);
    g.dots_spawned = 0;
    g.dots_total = p.dots;
    g.spawn_timer_ms = FIRST_SPAWN_MS;
    setState(g, STATE.PLAYING);
  }

  function newGame(g) {
    g.level = 1;
    g.lives = MAX_LIVES;
    g.score = 0;
    g.events |= EV.START;
    startCountdown(g, COUNTDOWN_FIRST_MS);
  }

  function zoneFlash(g, ms) {
    g.zone_flash_ms = ms;
    g.zone_flash_total_ms = ms;
  }

  function addFlash(g, led, color, ms) {
    var best = 0;
    for (var i = 0; i < MAX_FLASHES; i++) {
      if (!g.flashes[i].active) { best = i; break; }
      if (g.flashes[i].ttl_ms < g.flashes[best].ttl_ms) best = i;
    }
    var f = g.flashes[best];
    f.active = true;
    f.led = led;
    f.color = color;
    f.ttl_ms = ms;
    f.total_ms = ms;
  }

  function loseLife(g, flashMs) {
    g.lives--;
    g.events |= EV.LIFE_LOST;
    zoneFlash(g, flashMs);
    if (g.lives <= 0) {
      g.lives = 0;
      clearDots(g);
      clearShots(g);
      g.events |= EV.GAME_OVER;
      setState(g, STATE.GAME_OVER);
    }
  }

  /* ---- Playing ---------------------------------------------------------- */

  function spawnDot(g) {
    var p = levelParams(g.level);
    var next = levelParams(g.level + 1);

    var crossMs = lerpI(p.cross_ms, next.cross_ms, g.dots_spawned, g.dots_total);
    var speed = idiv(playLen(g) * ONE * 1000, crossMs);
    if (p.speed_var_pct > 0) {
      var pct = 100 - p.speed_var_pct + rngRange(g, 2 * p.speed_var_pct + 1);
      speed = idiv(speed * pct, 100);
    }

    var mask;
    var r = rngRange(g, 100);
    if (r < p.triple_pct) {
      mask = WHITE;
    } else if (r < p.triple_pct + p.mixed_pct) {
      mask = SECONDARIES[rngRange(g, 3)];
    } else {
      mask = PRIMARIES[rngRange(g, 3)];
    }

    for (var i = 0; i < MAX_DOTS; i++) {
      var d = g.dots[i];
      if (d.active) continue;
      d.active = true;
      d.mask = mask;
      d.colors_at_spawn = popcount3(mask);
      d.pos = g.n_leds * ONE;
      d.speed = speed;
      break;
    }
    g.dots_spawned++;
    g.spawn_timer_ms = p.gap_min_ms + rngRange(g, p.gap_max_ms - p.gap_min_ms + 1);
  }

  function spawnPointFree(g) {
    var limit = (g.n_leds - SPAWN_SPACING_LEDS - dotLen(g)) * ONE;
    for (var i = 0; i < MAX_DOTS; i++) {
      if (g.dots[i].active && g.dots[i].pos > limit) return false;
    }
    return true;
  }

  function resolveHit(g, s, d) {
    var led = idiv(d.pos, ONE);
    if (d.mask & s.mask) {
      d.mask &= ~s.mask;
      if (d.mask === 0) {
        g.score += POINTS_PER_COLOR * d.colors_at_spawn * g.level;
        addFlash(g, led, COL_CLEARED, FLASH_CLEAR_MS);
        g.events |= EV.DOT_CLEARED;
        d.active = false;
      } else {
        addFlash(g, led, colorForMask(s.mask), FLASH_REDUCE_MS);
        g.events |= EV.DOT_REDUCED;
      }
    } else {
      g.events |= EV.WRONG_HIT;
      addFlash(g, led, COL_FLASH_RED, FLASH_REDUCE_MS);
      if (!g.opt.wrong_hit_keeps_dot) d.active = false;
      loseLife(g, ZONE_FLASH_WRONG_MS);
    }
  }

  function tickPlaying(g, dt) {
    var p = levelParams(g.level);
    var i, j, d, s;

    g.spawn_timer_ms -= dt;
    if (g.dots_spawned < g.dots_total && g.spawn_timer_ms <= 0 &&
        activeDots(g) < p.max_concurrent && spawnPointFree(g)) {
      spawnDot(g);
    }

    var end = g.n_leds * ONE;
    for (i = 0; i < MAX_SHOTS; i++) {
      s = g.shots[i];
      if (!s.active) continue;
      s.pos += idiv(s.speed * dt, 1000);
      if (s.pos >= end) s.active = false;
    }
    for (i = 0; i < MAX_DOTS; i++) {
      d = g.dots[i];
      if (d.active) d.pos -= idiv(d.speed * dt, 1000);
    }

    for (i = 0; i < MAX_SHOTS; i++) {
      s = g.shots[i];
      if (!s.active) continue;
      var head = s.pos + ONE;
      var hit = null;
      for (j = 0; j < MAX_DOTS; j++) {
        d = g.dots[j];
        if (!d.active || d.pos > head) continue;
        if (hit === null || d.pos < hit.pos) hit = d;
      }
      if (hit === null) continue;
      s.active = false;
      resolveHit(g, s, hit);
      if (g.state !== STATE.PLAYING) return;
    }

    for (i = 0; i < MAX_DOTS; i++) {
      d = g.dots[i];
      if (!d.active || d.pos >= PLAY_START * ONE) continue;
      d.active = false;
      g.events |= EV.DOT_ESCAPED;
      loseLife(g, ZONE_FLASH_ESCAPE_MS);
      if (g.state !== STATE.PLAYING) return;
    }

    if (g.dots_spawned >= g.dots_total && activeDots(g) === 0) {
      g.score += 50 * g.level + 25 * g.lives;
      g.lives = Math.min(g.lives + 1, MAX_LIVES);
      clearShots(g);
      g.events |= EV.LEVEL_CLEAR;
      setState(g, STATE.LEVEL_CLEAR);
    }
  }

  /* ---- Public API ------------------------------------------------------- */

  function init(nLeds, seed, opt) {
    var g = {
      n_leds: Math.max(MIN_LEDS, Math.min(MAX_LEDS, nLeds | 0)),
      opt: { wrong_hit_keeps_dot: !!(opt && opt.wrong_hit_keeps_dot) },
      rng: (seed >>> 0) || 0x9E3779B9,
      state: STATE.ATTRACT, state_ms: 0, countdown_ms: 0, time_ms: 0,
      level: 1, lives: MAX_LIVES, score: 0,
      dots_spawned: 0, dots_total: 0, spawn_timer_ms: 0,
      zone_flash_ms: 0, zone_flash_total_ms: 0,
      dots: [], shots: [], flashes: [],
      events: 0
    };
    clearField(g);
    enterAttract(g);
    return g;
  }

  function tick(g, dtMs) {
    var dt = Math.max(1, Math.min(DT_MAX_MS, dtMs | 0));
    g.time_ms = (g.time_ms + dt) >>> 0;
    g.state_ms += dt;

    if (g.zone_flash_ms > 0) g.zone_flash_ms = Math.max(0, g.zone_flash_ms - dt);
    for (var i = 0; i < MAX_FLASHES; i++) {
      var f = g.flashes[i];
      if (!f.active) continue;
      f.ttl_ms -= dt;
      if (f.ttl_ms <= 0) f.active = false;
    }

    switch (g.state) {
      case STATE.ATTRACT:
        break;
      case STATE.COUNTDOWN:
        if (g.state_ms >= g.countdown_ms) startLevel(g);
        break;
      case STATE.PLAYING:
        tickPlaying(g, dt);
        break;
      case STATE.LEVEL_CLEAR:
        if (g.state_ms >= LEVEL_CLEAR_MS) {
          g.level++;
          startCountdown(g, COUNTDOWN_NEXT_MS);
        }
        break;
      case STATE.GAME_OVER:
        if (g.state_ms >= GAME_OVER_MS) enterAttract(g);
        break;
    }
  }

  function press(g, color) {
    if (color !== RED && color !== YELLOW && color !== BLUE) return;

    switch (g.state) {
      case STATE.ATTRACT:
        newGame(g);
        break;
      case STATE.GAME_OVER:
        if (g.state_ms >= GAME_OVER_RESTART_MS) newGame(g);
        break;
      case STATE.PLAYING:
        for (var i = 0; i < MAX_SHOTS; i++) {
          var s = g.shots[i];
          if (s.active) continue;
          s.active = true;
          s.mask = color;
          s.pos = PLAY_START * ONE;
          s.speed = idiv(playLen(g) * ONE * 1000, SHOT_CROSS_MS);
          g.events |= EV.SHOT;
          break;
        }
        break;
      default:
        break;
    }
  }

  function takeEvents(g) {
    var ev = g.events;
    g.events = 0;
    return ev;
  }

  /* ---- Rendering: out is a Uint8Array of 3 * n_leds bytes (r, g, b) ----- */

  function addPx(g, out, led, c, cov) {
    if (led < 0 || led >= g.n_leds || cov <= 0) return;
    if (cov > ONE) cov = ONE;
    var i = led * 3;
    out[i] = Math.min(255, out[i] + idiv(c.r * cov, ONE));
    out[i + 1] = Math.min(255, out[i + 1] + idiv(c.g * cov, ONE));
    out[i + 2] = Math.min(255, out[i + 2] + idiv(c.b * cov, ONE));
  }

  function addSegment(g, out, a, b, c, lo, hi) {
    if (b <= a) return;
    var first = Math.max(lo, idiv(a, ONE));
    var last = Math.min(hi, idiv(b - 1, ONE));
    for (var i = first; i <= last; i++) {
      var s = Math.max(a, i * ONE);
      var e = Math.min(b, (i + 1) * ONE);
      addPx(g, out, i, c, e - s);
    }
  }

  function setPx(out, led, c) {
    out[led * 3] = c.r;
    out[led * 3 + 1] = c.g;
    out[led * 3 + 2] = c.b;
  }

  function renderLives(g, out) {
    var lives = g.state === STATE.ATTRACT ? MAX_LIVES : g.lives;
    for (var i = 0; i < LIVES_LEDS && i < lives; i++) setPx(out, i, COL_LIFE);
  }

  function renderAttract(g, out) {
    var len = playLen(g) * ONE;
    var spacing = idiv(len, ATTRACT_COMETS);
    var phase = idiv((g.time_ms % ATTRACT_PERIOD_MS) * spacing, ATTRACT_PERIOD_MS);
    for (var j = 0; j < ATTRACT_COMETS; j++) {
      var head = PLAY_START * ONE + (j * spacing + (len - phase)) % len;
      var c = scale(colorForMask(WHEEL[j]), 6, 10);
      var led = idiv(head, ONE);
      for (var k = 0; k < COMET_TAIL; k++) {
        if (led + k >= g.n_leds) break;
        addPx(g, out, led + k, c, idiv((COMET_TAIL - k) * ONE, COMET_TAIL));
      }
    }
  }

  function renderCountdown(g, out) {
    var bar = zoneLen(g) * ONE;
    var remaining = Math.max(0, g.countdown_ms - g.state_ms);
    var lit = idiv(bar * remaining, g.countdown_ms);
    var a = PLAY_START * ONE;
    addSegment(g, out, a, a + lit, COL_COUNTDOWN, PLAY_START, g.n_leds - 1);
  }

  function renderPlaying(g, out) {
    var lo = PLAY_START, hi = g.n_leds - 1;
    var dl = dotLen(g) * ONE;
    var i;
    for (i = 0; i < MAX_DOTS; i++) {
      var d = g.dots[i];
      if (!d.active) continue;
      addSegment(g, out, d.pos, d.pos + dl, colorForMask(d.mask), lo, hi);
    }
    for (i = 0; i < MAX_SHOTS; i++) {
      var s = g.shots[i];
      if (!s.active) continue;
      var c = colorForMask(s.mask);
      addSegment(g, out, s.pos, s.pos + ONE, c, lo, hi);
      addSegment(g, out, s.pos - ONE, s.pos, scale(c, 4, 10), lo, hi);
    }
  }

  function renderLevelNumber(g, out, level, tMs) {
    if (idiv(tMs, BLINK_MS) % 2 !== 0) return;
    var n = Math.min(level, playLen(g));
    for (var i = 0; i < n; i++) setPx(out, PLAY_START + i, COL_LEVEL);
  }

  function renderLevelClear(g, out) {
    if (g.state_ms < LEVEL_CLEAR_RAINBOW_MS) {
      var len = Math.min(RAINBOW_MAX_LEDS, idiv(playLen(g), 4));
      var head = PLAY_START + idiv(playLen(g) * g.state_ms, LEVEL_CLEAR_RAINBOW_MS);
      for (var k = 0; k < len; k++) {
        var led = head - k;
        if (led < PLAY_START || led >= g.n_leds) continue;
        var c = colorForMask(WHEEL[idiv(k * 6, len) % 6]);
        addPx(g, out, led, c, idiv((len - k) * ONE, len));
      }
    } else {
      renderLevelNumber(g, out, g.level + 1, g.state_ms - LEVEL_CLEAR_RAINBOW_MS);
    }
  }

  function renderGameOver(g, out) {
    if (g.state_ms < GAME_OVER_PULSE_MS) {
      var v = g.state_ms % GAME_OVER_PULSE_PERIOD;
      var half = idiv(GAME_OVER_PULSE_PERIOD, 2);
      var inten = v < half ? idiv(v * ONE, half) : idiv((GAME_OVER_PULSE_PERIOD - v) * ONE, half);
      var n = PLAY_START + zoneLen(g);
      for (var i = 0; i < n; i++) addPx(g, out, i, COL_FLASH_RED, inten);
    } else {
      renderLevelNumber(g, out, g.level, g.state_ms - GAME_OVER_PULSE_MS);
    }
  }

  function renderOverlays(g, out) {
    var i;
    if (g.zone_flash_ms > 0) {
      var inten = idiv(g.zone_flash_ms * ONE, g.zone_flash_total_ms);
      var n = PLAY_START + zoneLen(g);
      for (i = 0; i < n; i++) addPx(g, out, i, COL_FLASH_RED, inten);
    }
    for (i = 0; i < MAX_FLASHES; i++) {
      var f = g.flashes[i];
      if (!f.active) continue;
      var fi = idiv(f.ttl_ms * ONE, f.total_ms);
      addPx(g, out, f.led, f.color, fi);
      if (f.led - 1 >= PLAY_START) addPx(g, out, f.led - 1, f.color, idiv(fi, 2));
      addPx(g, out, f.led + 1, f.color, idiv(fi, 2));
    }
  }

  function render(g, out) {
    if (!out) out = new Uint8Array(g.n_leds * 3);
    out.fill(0, 0, g.n_leds * 3);
    renderLives(g, out);
    switch (g.state) {
      case STATE.ATTRACT:     renderAttract(g, out); break;
      case STATE.COUNTDOWN:   renderCountdown(g, out); break;
      case STATE.PLAYING:     renderPlaying(g, out); break;
      case STATE.LEVEL_CLEAR: renderLevelClear(g, out); break;
      case STATE.GAME_OVER:   renderGameOver(g, out); break;
    }
    renderOverlays(g, out);
    return out;
  }

  return {
    MIN_LEDS: MIN_LEDS, MAX_LEDS: MAX_LEDS, LIVES_LEDS: LIVES_LEDS, PLAY_START: PLAY_START,
    MAX_LIVES: MAX_LIVES, MAX_DOTS: MAX_DOTS, MAX_SHOTS: MAX_SHOTS, LEVEL_COUNT: LEVEL_COUNT,
    RED: RED, YELLOW: YELLOW, BLUE: BLUE, ORANGE: ORANGE, PURPLE: PURPLE, GREEN: GREEN, WHITE: WHITE,
    STATE: STATE, EV: EV, LEVELS: LEVELS,
    init: init, tick: tick, press: press, takeEvents: takeEvents, render: render,
    colorForMask: colorForMask, levelParams: levelParams, stateName: stateName, activeDots: activeDots
  };
});
