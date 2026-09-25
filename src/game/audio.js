// Procedural WebAudio: synthesized SFX with 3D panning & city reverb, vehicle engine/tires/wind,
// sirens, helicopter, ambience (traffic, birds, crickets, waves, rain) and generative radio stations.
import * as THREE from 'three';
import { clamp, rand, pick } from '../core/utils.js';

const _v = new THREE.Vector3(), _f = new THREE.Vector3(), _u = new THREE.Vector3();

export class Audio {
  constructor(game) {
    this.game = game;
    this.ctx = null;
    this.enabled = false;
    this.volume = game.settings.volume ?? 0.8;
    this.musicVolume = game.settings.music ?? 0.6;
    this.loops = new Set();
    this.station = 0;
    this.radioOn = true;
  }

  // Must be called from a user gesture
  init() {
    if (this.ctx) { if (this.ctx.state === 'suspended') this.ctx.resume(); return; }
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return;
    const ctx = new Ctx();
    this.ctx = ctx;
    this.master = ctx.createGain();
    this.master.gain.value = this.volume;
    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -14; comp.ratio.value = 4; comp.attack.value = 0.003; comp.release.value = 0.2;
    // world filter: muffles everything (except stingers) on death / busted
    this.worldFilter = ctx.createBiquadFilter();
    this.worldFilter.type = 'lowpass'; this.worldFilter.frequency.value = 20000; this.worldFilter.Q.value = 0.5;
    this.master.connect(this.worldFilter).connect(comp).connect(ctx.destination);
    // stingers bypass the world filter
    this.stinger = ctx.createGain(); this.stinger.gain.value = this.volume; this.stinger.connect(ctx.destination);
    this.samples = {};
    this.loadSample('wasted', 'assets/audio/wasted.mp3');
    this.sfx = ctx.createGain(); this.sfx.connect(this.master);
    this.music = ctx.createGain(); this.music.gain.value = this.musicVolume * 0.55; this.music.connect(this.master);
    this.amb = ctx.createGain(); this.amb.gain.value = 0.5; this.amb.connect(this.master);
    // reverb bus
    this.reverb = ctx.createConvolver();
    this.reverb.buffer = this._impulse(2.2, 2.5);
    this.revSend = ctx.createGain(); this.revSend.gain.value = 0.35;
    this.revSend.connect(this.reverb).connect(this.sfx);
    // noise buffers
    this.noise = this._noiseBuffer(2, 'white');
    this.brown = this._noiseBuffer(4, 'brown');
    this.pink = this._noiseBuffer(3, 'pink');
    this.enabled = true;
    this._setupVehicleAudio();
    this._setupAmbience();
    this.radio = new Radio(this);
  }

  setVolume(v) { this.volume = v; if (this.master) this.master.gain.value = v; if (this.stinger) this.stinger.gain.value = v; }

  // ---------------------------------------------------------------- recorded samples
  async loadSample(name, url) {
    try {
      const res = await fetch(url);
      if (!res.ok) throw new Error(res.status);
      this.samples[name] = await this.ctx.decodeAudioData(await res.arrayBuffer());
    } catch (e) { this.samples[name] = null; }
  }
  // plays a loaded sample on the stinger bus; returns false if it isn't available (caller falls back)
  playSample(name, vol = 1) {
    if (!this.enabled) return false;
    const buf = this.samples[name];
    if (!buf) return false;
    this.stopSample(name);
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    const g = this.ctx.createGain(); g.gain.value = vol;
    src.connect(g).connect(this.stinger);
    src.start();
    this._playing = this._playing || {};
    this._playing[name] = { src, g };
    src.onended = () => { if (this._playing[name]?.src === src) delete this._playing[name]; };
    return true;
  }
  stopSample(name, fade = 0) {
    const p = this._playing?.[name];
    if (!p) return;
    const t = this.ctx.currentTime;
    if (fade > 0) { p.g.gain.setValueAtTime(p.g.gain.value, t); p.g.gain.linearRampToValueAtTime(0, t + fade); p.src.stop(t + fade + 0.05); }
    else p.src.stop();
    delete this._playing[name];
  }
  // muffle the world (death / arrest) and duck the radio
  muffle(on) {
    if (!this.enabled) return;
    const t = this.ctx.currentTime;
    const f = this.worldFilter.frequency;
    f.cancelScheduledValues(t); f.setValueAtTime(f.value, t);
    f.exponentialRampToValueAtTime(on ? 420 : 20000, t + (on ? 0.6 : 1.2));
    const m = this.music.gain;
    m.cancelScheduledValues(t); m.setValueAtTime(m.value, t);
    m.linearRampToValueAtTime(on ? this.musicVolume * 0.12 : this.musicVolume * 0.55, t + (on ? 0.5 : 1.5));
  }
  setMusic(v) { this.musicVolume = v; if (this.music) this.music.gain.value = v * 0.55; }

  _noiseBuffer(sec, type) {
    const ctx = this.ctx;
    const len = Math.floor(ctx.sampleRate * sec);
    const buf = ctx.createBuffer(1, len, ctx.sampleRate);
    const d = buf.getChannelData(0);
    let last = 0, b0 = 0, b1 = 0, b2 = 0;
    for (let i = 0; i < len; i++) {
      const w = Math.random() * 2 - 1;
      if (type === 'brown') { last = (last + 0.02 * w) / 1.02; d[i] = last * 3.5; }
      else if (type === 'pink') { b0 = 0.99765 * b0 + w * 0.099046; b1 = 0.963 * b1 + w * 0.2965164; b2 = 0.57 * b2 + w * 1.0526913; d[i] = (b0 + b1 + b2 + w * 0.1848) * 0.2; }
      else d[i] = w;
    }
    return buf;
  }
  _impulse(sec, decay) {
    const ctx = this.ctx;
    const len = Math.floor(ctx.sampleRate * sec);
    const buf = ctx.createBuffer(2, len, ctx.sampleRate);
    for (let c = 0; c < 2; c++) {
      const d = buf.getChannelData(c);
      for (let i = 0; i < len; i++) {
        const t = i / len;
        // early reflections off buildings + diffuse tail
        const er = (i % Math.floor(ctx.sampleRate * (0.031 + c * 0.007)) < 30) ? 0.6 : 0;
        d[i] = ((Math.random() * 2 - 1) * 0.5 + er * (Math.random() - 0.5)) * Math.pow(1 - t, decay);
      }
    }
    return buf;
  }

  // ------------------------------------------------------------------ primitives
  _src(buf, loop = false) { const s = this.ctx.createBufferSource(); s.buffer = buf; s.loop = loop; return s; }
  _env(g, t, a, peak, d, sustain = 0, rel = 0) {
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(Math.max(peak, 0.0002), t + a);
    g.gain.exponentialRampToValueAtTime(Math.max(sustain, 0.0001), t + a + d);
    if (rel) g.gain.exponentialRampToValueAtTime(0.0001, t + a + d + rel);
  }
  _noiseHit(dest, t, { dur = 0.2, vol = 1, type = 'highpass', freq = 1000, q = 0.7, attack = 0.001, sweepTo = null, buf = null, offset = null }) {
    const ctx = this.ctx;
    const s = this._src(buf || this.noise);
    const f = ctx.createBiquadFilter(); f.type = type; f.frequency.setValueAtTime(freq, t); f.Q.value = q;
    if (sweepTo) f.frequency.exponentialRampToValueAtTime(sweepTo, t + dur);
    const g = ctx.createGain();
    this._env(g, t, attack, vol, dur);
    s.connect(f).connect(g).connect(dest);
    s.start(t, offset ?? Math.random() * 1.5);
    s.stop(t + dur + attack + 0.05);
    return g;
  }
  _tone(dest, t, { f0 = 100, f1 = null, dur = 0.2, vol = 0.5, type = 'sine', attack = 0.002 }) {
    const ctx = this.ctx;
    const o = ctx.createOscillator(); o.type = type; o.frequency.setValueAtTime(f0, t);
    if (f1) o.frequency.exponentialRampToValueAtTime(Math.max(1, f1), t + dur);
    const g = ctx.createGain();
    this._env(g, t, attack, vol, dur);
    o.connect(g).connect(dest);
    o.start(t); o.stop(t + dur + attack + 0.05);
    return o;
  }

  _panner(pos, ref = 6, max = 400) {
    const p = this.ctx.createPanner();
    p.panningModel = 'HRTF'; p.distanceModel = 'inverse';
    p.refDistance = ref; p.maxDistance = max; p.rolloffFactor = 1.1;
    p.positionX.value = pos.x; p.positionY.value = pos.y; p.positionZ.value = pos.z;
    return p;
  }

  play(name, vol = 1) { if (!this.enabled) return; this._make(name, this.sfx, vol); }

  playAt(name, pos, vol = 1, opts = {}) {
    if (!this.enabled || !pos) return;
    const cam = this.game.camera.position;
    const d = cam.distanceTo(pos);
    if (d > (opts.gun ? 500 : 180)) return;
    const pan = this._panner(pos, opts.gun ? 12 : 5);
    pan.connect(this.sfx);
    if (opts.gun || name === 'explosion') {
      const send = this.ctx.createGain(); send.gain.value = clamp(0.3 + d / 150, 0.3, 1.4);
      pan.connect(send).connect(this.revSend);
    }
    this._make(name, pan, vol, d);
  }

  _make(name, dest, vol, dist = 0) {
    const t = this.ctx.currentTime + 0.005;
    const far = clamp(dist / 200, 0, 1);
    switch (name) {
      case 'pistol':
        this._noiseHit(dest, t, { dur: 0.18, vol: 1.2 * vol, freq: 900 - far * 600, type: 'highpass' });
        this._noiseHit(dest, t, { dur: 0.06, vol: 0.9 * vol, freq: 3000, type: 'bandpass', q: 0.8 });
        this._tone(dest, t, { f0: 160, f1: 45, dur: 0.12, vol: 1.1 * vol });
        break;
      case 'smg':
        this._noiseHit(dest, t, { dur: 0.09, vol: 0.9 * vol, freq: 1300 - far * 800, type: 'highpass' });
        this._tone(dest, t, { f0: 190, f1: 60, dur: 0.07, vol: 0.7 * vol });
        break;
      case 'rifle':
        this._noiseHit(dest, t, { dur: 0.14, vol: 1.2 * vol, freq: 700 - far * 400, type: 'highpass' });
        this._noiseHit(dest, t, { dur: 0.04, vol: 1.0 * vol, freq: 4500, type: 'bandpass', q: 1 });
        this._tone(dest, t, { f0: 140, f1: 40, dur: 0.11, vol: 1.2 * vol });
        break;
      case 'shotgun':
        this._noiseHit(dest, t, { dur: 0.35, vol: 1.5 * vol, freq: 400, type: 'lowpass', sweepTo: 150 });
        this._noiseHit(dest, t, { dur: 0.12, vol: 1.1 * vol, freq: 1500, type: 'highpass' });
        this._tone(dest, t, { f0: 110, f1: 35, dur: 0.25, vol: 1.4 * vol });
        this._noiseHit(dest, t + 0.45, { dur: 0.05, vol: 0.3 * vol, freq: 2500, type: 'bandpass', q: 3 });
        this._noiseHit(dest, t + 0.58, { dur: 0.05, vol: 0.35 * vol, freq: 2000, type: 'bandpass', q: 3 });
        break;
      case 'rpg':
        this._noiseHit(dest, t, { dur: 0.9, vol: 1.0 * vol, freq: 300, type: 'bandpass', sweepTo: 3000, q: 1.5, attack: 0.02 });
        this._tone(dest, t, { f0: 80, f1: 40, dur: 0.3, vol: 0.9 * vol });
        break;
      case 'explosion': {
        this._noiseHit(dest, t, { dur: 2.5, vol: 2.0 * vol, freq: 3000, type: 'lowpass', sweepTo: 80, buf: this.brown, attack: 0.005 });
        this._noiseHit(dest, t, { dur: 0.6, vol: 1.5 * vol, freq: 1200, type: 'lowpass', sweepTo: 200, attack: 0.002 });
        this._tone(dest, t, { f0: 70, f1: 25, dur: 1.2, vol: 2.0 * vol });
        for (let i = 0; i < 6; i++) this._noiseHit(dest, t + 0.2 + Math.random() * 1.2, { dur: 0.05, vol: 0.3 * vol, freq: 2000 + Math.random() * 3000, type: 'bandpass', q: 2 });
        break;
      }
      case 'crash': {
        this._noiseHit(dest, t, { dur: 0.35, vol: 1.3 * vol, freq: 600, type: 'lowpass', sweepTo: 150 });
        this._noiseHit(dest, t, { dur: 0.2, vol: 0.8 * vol, freq: 2500, type: 'bandpass', q: 1.5 });
        const fs = [233, 347, 519, 787, 1123];
        for (const f of fs) this._tone(dest, t, { f0: f * rand(0.9, 1.1), dur: rand(0.2, 0.6), vol: 0.12 * vol, type: 'triangle' });
        for (let i = 0; i < 4; i++) this._noiseHit(dest, t + 0.05 + Math.random() * 0.3, { dur: 0.04, vol: 0.25 * vol, freq: 4000, type: 'bandpass', q: 4 });
        break;
      }
      case 'metalhit':
        for (const f of [412, 689, 1033]) this._tone(dest, t, { f0: f, dur: 0.3, vol: 0.12 * vol, type: 'triangle' });
        this._noiseHit(dest, t, { dur: 0.08, vol: 0.5 * vol, freq: 1800, type: 'bandpass' });
        break;
      case 'punch':
        this._tone(dest, t, { f0: 120, f1: 50, dur: 0.1, vol: 1.0 * vol });
        this._noiseHit(dest, t, { dur: 0.06, vol: 0.7 * vol, freq: 800, type: 'lowpass' });
        break;
      case 'bat':
        this._tone(dest, t, { f0: 180, f1: 60, dur: 0.12, vol: 1.0 * vol });
        this._noiseHit(dest, t, { dur: 0.08, vol: 0.8 * vol, freq: 1200, type: 'bandpass' });
        break;
      case 'stab':
        this._noiseHit(dest, t, { dur: 0.12, vol: 0.6 * vol, freq: 900, type: 'bandpass', q: 2 });
        this._tone(dest, t, { f0: 90, f1: 60, dur: 0.08, vol: 0.5 * vol });
        break;
      case 'swoosh':
        this._noiseHit(dest, t, { dur: 0.22, vol: 0.35 * vol, freq: 500, type: 'bandpass', sweepTo: 2500, q: 2, attack: 0.05 });
        break;
      case 'bodyhit':
        this._tone(dest, t, { f0: 80, f1: 40, dur: 0.2, vol: 1.2 * vol });
        this._noiseHit(dest, t, { dur: 0.15, vol: 0.6 * vol, freq: 500, type: 'lowpass' });
        break;
      case 'bulletflesh':
        this._noiseHit(dest, t, { dur: 0.05, vol: 0.4 * vol, freq: 700, type: 'lowpass' });
        break;
      case 'bulletmetal':
        this._tone(dest, t, { f0: rand(1500, 2500), dur: 0.08, vol: 0.2 * vol, type: 'triangle' });
        this._noiseHit(dest, t, { dur: 0.03, vol: 0.4 * vol, freq: 3000, type: 'highpass' });
        break;
      case 'ricochet': {
        const o = this._tone(dest, t, { f0: rand(2200, 3200), f1: rand(600, 900), dur: 0.35, vol: 0.12 * vol, type: 'sine' });
        break;
      }
      case 'clink': this._tone(dest, t, { f0: 2400, dur: 0.06, vol: 0.2 * vol, type: 'triangle' }); break;
      case 'dryfire': this._noiseHit(dest, t, { dur: 0.03, vol: 0.4 * vol, freq: 3000, type: 'bandpass', q: 5 }); break;
      case 'reload':
        this._noiseHit(dest, t, { dur: 0.04, vol: 0.5 * vol, freq: 2500, type: 'bandpass', q: 4 });
        this._noiseHit(dest, t + 0.35, { dur: 0.05, vol: 0.6 * vol, freq: 1800, type: 'bandpass', q: 4 });
        break;
      case 'switch': this._noiseHit(dest, t, { dur: 0.04, vol: 0.35 * vol, freq: 2200, type: 'bandpass', q: 3 }); break;
      case 'doorOpen': this._noiseHit(dest, t, { dur: 0.08, vol: 0.4 * vol, freq: 600, type: 'lowpass' }); break;
      case 'doorClose':
        this._tone(dest, t, { f0: 110, f1: 70, dur: 0.12, vol: 0.8 * vol });
        this._noiseHit(dest, t, { dur: 0.08, vol: 0.5 * vol, freq: 900, type: 'lowpass' });
        break;
      case 'locked': this._tone(dest, t, { f0: 300, dur: 0.08, vol: 0.2 * vol, type: 'square' }); break;
      case 'horn':
        for (const f of [392, 494]) { const o = this._tone(dest, t, { f0: f, dur: 0.5, vol: 0.18 * vol, type: 'square', attack: 0.01 }); }
        break;
      case 'pickup':
        [0, 4, 7, 12].forEach((n, i) => this._tone(dest, t + i * 0.06, { f0: 523.25 * Math.pow(2, n / 12), dur: 0.18, vol: 0.25 * vol, type: 'triangle' }));
        break;
      case 'cash':
        [12, 16, 19].forEach((n, i) => this._tone(dest, t + i * 0.05, { f0: 523.25 * Math.pow(2, n / 12), dur: 0.12, vol: 0.2 * vol, type: 'square' }));
        break;
      case 'ui': this._tone(dest, t, { f0: 880, dur: 0.05, vol: 0.15 * vol, type: 'triangle' }); break;
      case 'checkpoint':
        [0, 7].forEach((n, i) => this._tone(dest, t + i * 0.08, { f0: 659 * Math.pow(2, n / 12), dur: 0.2, vol: 0.25 * vol, type: 'triangle' }));
        break;
      case 'passed': {
        const seq = [[0, 0.0], [4, 0.15], [7, 0.3], [12, 0.45], [7, 0.75], [12, 0.9], [16, 1.05], [19, 1.2]];
        for (const [n, dt] of seq) {
          this._tone(dest, t + dt, { f0: 392 * Math.pow(2, n / 12), dur: 0.35, vol: 0.22 * vol, type: 'sawtooth', attack: 0.01 });
          this._tone(dest, t + dt, { f0: 196 * Math.pow(2, n / 12), dur: 0.35, vol: 0.18 * vol, type: 'triangle', attack: 0.01 });
        }
        this._tone(dest, t + 1.4, { f0: 392 * 2, dur: 1.2, vol: 0.25 * vol, type: 'sawtooth', attack: 0.02 });
        break;
      }
      case 'failed':
        [[0, 0], [-3, 0.3], [-7, 0.6]].forEach(([n, dt]) => this._tone(dest, t + dt, { f0: 220 * Math.pow(2, n / 12), dur: 0.5, vol: 0.25 * vol, type: 'sawtooth' }));
        break;
      case 'wasted':
        this._tone(dest, t, { f0: 110, f1: 55, dur: 2.5, vol: 0.5 * vol, type: 'sawtooth' });
        this._tone(dest, t, { f0: 116, f1: 58, dur: 2.5, vol: 0.4 * vol, type: 'sawtooth' });
        break;
      case 'wanted': this._tone(dest, t, { f0: 740, dur: 0.1, vol: 0.2 * vol, type: 'square' }); this._tone(dest, t + 0.12, { f0: 988, dur: 0.12, vol: 0.2 * vol, type: 'square' }); break;
      case 'footstep': this._noiseHit(dest, t, { dur: 0.05, vol: 0.12 * vol, freq: 1000, type: 'bandpass', q: 1.5 }); break;
      case 'splash': this._noiseHit(dest, t, { dur: 0.8, vol: 0.8 * vol, freq: 800, type: 'lowpass', sweepTo: 200, buf: this.pink }); break;
      case 'thunder': this._noiseHit(dest, t, { dur: 4, vol: 1.2 * vol, freq: 400, type: 'lowpass', sweepTo: 60, buf: this.brown, attack: 0.1 }); break;
      default: break;
    }
  }

  // positional looping sound handle
  loop(name, pos) {
    if (!this.enabled) return null;
    const ctx = this.ctx;
    const pan = this._panner(pos, 15, 600);
    pan.connect(this.sfx);
    const g = ctx.createGain(); g.gain.value = 0.8; g.connect(pan);
    const nodes = [];
    if (name === 'heli') {
      const s = this._src(this.brown, true);
      const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 500;
      const am = ctx.createGain(); am.gain.value = 0.5;
      const lfo = ctx.createOscillator(); lfo.frequency.value = 14; const lg = ctx.createGain(); lg.gain.value = 0.5;
      lfo.connect(lg).connect(am.gain);
      s.connect(f).connect(am).connect(g);
      s.start(); lfo.start();
      nodes.push(s, lfo);
    } else if (name === 'siren') {
      const o = ctx.createOscillator(); o.type = 'sawtooth';
      const lfo = ctx.createOscillator(); lfo.frequency.value = 0.35; const lg = ctx.createGain(); lg.gain.value = 350;
      o.frequency.value = 950;
      lfo.connect(lg).connect(o.frequency);
      const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 2200;
      const gg = ctx.createGain(); gg.gain.value = 0.15;
      o.connect(f).connect(gg).connect(g);
      o.start(); lfo.start();
      nodes.push(o, lfo);
    } else if (name === 'fire') {
      const s = this._src(this.pink, true);
      const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 900;
      s.connect(f).connect(g); s.start();
      nodes.push(s);
    }
    const handle = {
      gain: g,
      setPos: (p) => { pan.positionX.value = p.x; pan.positionY.value = p.y; pan.positionZ.value = p.z; },
      setVol: (v) => { g.gain.setTargetAtTime(v, ctx.currentTime, 0.1); },
      stop: () => { for (const n of nodes) { try { n.stop(); } catch { /* */ } } g.disconnect(); pan.disconnect(); this.loops.delete(handle); },
    };
    this.loops.add(handle);
    return handle;
  }

  // ------------------------------------------------------------------ vehicle audio (player car)
  _setupVehicleAudio() {
    const ctx = this.ctx;
    const out = ctx.createGain(); out.gain.value = 0; out.connect(this.sfx);
    const o1 = ctx.createOscillator(); o1.type = 'sawtooth';
    const o2 = ctx.createOscillator(); o2.type = 'square';
    const o3 = ctx.createOscillator(); o3.type = 'sine';
    const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 400; f.Q.value = 3;
    const g1 = ctx.createGain(); g1.gain.value = 0.35;
    const g2 = ctx.createGain(); g2.gain.value = 0.2;
    const g3 = ctx.createGain(); g3.gain.value = 0.5;
    o1.connect(g1).connect(f); o2.connect(g2).connect(f); o3.connect(g3).connect(f);
    const rumble = this._src(this.brown, true);
    const rf = ctx.createBiquadFilter(); rf.type = 'lowpass'; rf.frequency.value = 180;
    const rg = ctx.createGain(); rg.gain.value = 0.4;
    rumble.connect(rf).connect(rg).connect(f);
    f.connect(out);
    o1.start(); o2.start(); o3.start(); rumble.start();
    // tires
    const tire = this._src(this.noise, true);
    const tf = ctx.createBiquadFilter(); tf.type = 'bandpass'; tf.frequency.value = 1800; tf.Q.value = 6;
    const tg = ctx.createGain(); tg.gain.value = 0;
    tire.connect(tf).connect(tg).connect(this.sfx); tire.start();
    // wind
    const wind = this._src(this.pink, true);
    const wf = ctx.createBiquadFilter(); wf.type = 'lowpass'; wf.frequency.value = 600;
    const wg = ctx.createGain(); wg.gain.value = 0;
    wind.connect(wf).connect(wg).connect(this.sfx); wind.start();
    // horn
    const h1 = ctx.createOscillator(); h1.type = 'square'; h1.frequency.value = 392;
    const h2 = ctx.createOscillator(); h2.type = 'square'; h2.frequency.value = 494;
    const hf = ctx.createBiquadFilter(); hf.type = 'lowpass'; hf.frequency.value = 1500;
    const hg = ctx.createGain(); hg.gain.value = 0;
    h1.connect(hf); h2.connect(hf); hf.connect(hg).connect(this.sfx); h1.start(); h2.start();
    this.veh = { out, o1, o2, o3, f, tg, tf, wg, hg, rpm: 800, gear: 1 };
    this.sirens = [];
  }

  _setupAmbience() {
    const ctx = this.ctx;
    const city = this._src(this.brown, true);
    const cf = ctx.createBiquadFilter(); cf.type = 'lowpass'; cf.frequency.value = 350;
    this.cityGain = ctx.createGain(); this.cityGain.gain.value = 0.25;
    city.connect(cf).connect(this.cityGain).connect(this.amb); city.start();
    const waves = this._src(this.pink, true);
    const wf = ctx.createBiquadFilter(); wf.type = 'lowpass'; wf.frequency.value = 700;
    this.waveGain = ctx.createGain(); this.waveGain.gain.value = 0;
    const lfo = ctx.createOscillator(); lfo.frequency.value = 0.12; const lg = ctx.createGain(); lg.gain.value = 0.4;
    const am = ctx.createGain(); am.gain.value = 0.6;
    lfo.connect(lg).connect(am.gain); lfo.start();
    waves.connect(wf).connect(am).connect(this.waveGain).connect(this.amb); waves.start();
    const rain = this._src(this.noise, true);
    const rf = ctx.createBiquadFilter(); rf.type = 'highpass'; rf.frequency.value = 2500;
    this.rainGain = ctx.createGain(); this.rainGain.gain.value = 0;
    rain.connect(rf).connect(this.rainGain).connect(this.amb); rain.start();
    this.birdT = 1; this.cricketT = 1;
  }

  _chirp(night) {
    const ctx = this.ctx, t = ctx.currentTime + 0.01;
    const cam = this.game.camera.position;
    const pos = _v.set(cam.x + rand(-30, 30), cam.y + rand(3, 12), cam.z + rand(-30, 30));
    const pan = this._panner(pos, 8);
    pan.connect(this.amb);
    if (night) {
      for (let i = 0; i < 6; i++) this._tone(pan, t + i * 0.07, { f0: 4200 + rand(-100, 100), dur: 0.04, vol: 0.04, type: 'sine' });
    } else {
      const base = rand(2200, 3800);
      for (let i = 0; i < randInt3(); i++) this._tone(pan, t + i * 0.12, { f0: base * rand(0.9, 1.2), f1: base * rand(1.2, 1.6), dur: 0.08, vol: 0.05, type: 'sine' });
    }
  }

  update(dt) {
    if (!this.enabled) return;
    const game = this.game;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    // listener
    const cam = game.camera;
    const L = ctx.listener;
    cam.getWorldDirection(_f);
    _u.set(0, 1, 0).applyQuaternion(cam.quaternion);
    if (L.positionX) {
      L.positionX.value = cam.position.x; L.positionY.value = cam.position.y; L.positionZ.value = cam.position.z;
      L.forwardX.value = _f.x; L.forwardY.value = _f.y; L.forwardZ.value = _f.z;
      L.upX.value = _u.x; L.upY.value = _u.y; L.upZ.value = _u.z;
    } else { L.setPosition(cam.position.x, cam.position.y, cam.position.z); L.setOrientation(_f.x, _f.y, _f.z, _u.x, _u.y, _u.z); }

    // player vehicle
    const pv = game.player.vehicle;
    const V = this.veh;
    if (pv && !pv.isWrecked) {
      const sp = Math.abs(pv.speed);
      const top = pv.def.top;
      // fake gearbox
      const ratio = sp / top;
      const gears = [0, 0.18, 0.34, 0.52, 0.72, 1.01];
      let gear = 1; while (gear < 5 && ratio > gears[gear]) gear++;
      const lo = gears[gear - 1], hi = gears[gear];
      let rpm = 900 + ((ratio - lo) / (hi - lo)) * 5200;
      if (pv.wheelspin > 0.1 || (pv.input.throttle > 0.5 && sp < 3)) rpm = Math.max(rpm, 3500 + pv.input.throttle * 2500);
      if (pv.airborne) rpm = Math.max(rpm, 5000 * pv.input.throttle + 1500);
      V.rpm += (rpm - V.rpm) * Math.min(1, dt * 8);
      const heavy = pv.def.mass > 2000 ? 0.7 : 1;
      const base = (V.rpm / 60) * 0.5 * heavy * (pv.def.body === 'super' ? 1.3 : pv.def.body === 'muscle' ? 0.8 : 1);
      V.o1.frequency.setTargetAtTime(base, t, 0.03);
      V.o2.frequency.setTargetAtTime(base * 0.5, t, 0.03);
      V.o3.frequency.setTargetAtTime(base * 0.25, t, 0.03);
      V.f.frequency.setTargetAtTime(250 + pv.input.throttle * 900 + V.rpm * 0.1, t, 0.05);
      V.out.gain.setTargetAtTime(0.13 + pv.input.throttle * 0.12, t, 0.05);
      const skid = pv.skid ? clamp(pv.slipRear / 8 + pv.wheelspin, 0.2, 1) : 0;
      V.tg.gain.setTargetAtTime(skid * 0.18, t, 0.04);
      V.tf.frequency.setTargetAtTime(1400 + sp * 20, t, 0.1);
      V.wg.gain.setTargetAtTime(clamp(sp / 50, 0, 1) * 0.12, t, 0.2);
      V.hg.gain.setTargetAtTime(pv.horn ? 0.08 : 0, t, 0.01);
    } else {
      V.out.gain.setTargetAtTime(0, t, 0.1);
      V.tg.gain.setTargetAtTime(0, t, 0.05);
      V.wg.gain.setTargetAtTime(0, t, 0.2);
      V.hg.gain.setTargetAtTime(0, t, 0.01);
    }
    // sirens: attach loop handles to nearest 2 siren cars
    const sirenCars = game.vehicles.list.filter((v) => v.sirenOn && !v.isWrecked).sort((a, b) => a.pos.distanceToSquared(cam.position) - b.pos.distanceToSquared(cam.position)).slice(0, 2);
    while (this.sirens.length < sirenCars.length) this.sirens.push(this.loop('siren', cam.position));
    while (this.sirens.length > sirenCars.length) this.sirens.pop()?.stop();
    sirenCars.forEach((v, i) => this.sirens[i]?.setPos(v.pos));
    // ambience
    const env = game.env;
    const night = env.night;
    const d = game.map.districtAt(cam.position.x, cam.position.z);
    this.cityGain.gain.setTargetAtTime((d === 'hills' ? 0.08 : 0.22) * (1 - night * 0.4), t, 0.5);
    const beachDist = Math.max(0, cam.position.z - 560);
    this.waveGain.gain.setTargetAtTime(clamp(beachDist / 120, 0, 1) * 0.35, t, 0.5);
    this.rainGain.gain.setTargetAtTime(env.rain * 0.18, t, 0.5);
    this.birdT -= dt; this.cricketT -= dt;
    if (!pv && night < 0.3 && this.birdT <= 0) { this.birdT = rand(1.5, 5); if (d === 'hills' || d === 'hood' || d === 'westside' || Math.random() < 0.3) this._chirp(false); }
    if (!pv && night > 0.6 && this.cricketT <= 0) { this.cricketT = rand(0.8, 2.5); this._chirp(true); }
    if (env.lightning > 0.95 && !this._thunderLock) { this._thunderLock = true; setTimeout(() => { this.play('thunder', 0.8); this._thunderLock = false; }, rand(300, 2500)); }
    // radio only while in a vehicle
    this.radio?.update(dt, !!pv && this.radioOn);
  }
}

function randInt3() { return 2 + Math.floor(Math.random() * 3); }

// ====================================================================================== radio
const STATIONS = [
  { name: 'Radio Los Soles', genre: 'West Coast Classics', bpm: 92, style: 'gfunk' },
  { name: 'Neon 88.8', genre: 'Synthwave', bpm: 108, style: 'synth' },
  { name: 'Low Rider Soul', genre: 'Slow Jams & Funk', bpm: 78, style: 'soul' },
  { name: 'Off', genre: '', style: 'off' },
];

class Radio {
  constructor(audio) {
    this.audio = audio;
    this.ctx = audio.ctx;
    this.out = this.ctx.createGain();
    this.out.gain.value = 0;
    this.out.connect(audio.music);
    this.station = 0;
    this.playing = false;
    this.nextTime = 0;
    this.step = 0;
    this.bar = 0;
    this.song = null;
    this.songBars = 0;
    this._newSong();
  }

  get current() { return STATIONS[this.station]; }
  next() {
    this.station = (this.station + 1) % STATIONS.length;
    this._newSong();
    this.nextTime = this.ctx.currentTime + 0.1;
    this.audio.game.hud?.showRadio(this.current);
  }

  _newSong() {
    const st = STATIONS[this.station];
    const roots = [0, 2, 3, 5, 7, 8, 10];
    const key = 36 + Math.floor(Math.random() * 7);
    const minor = [0, 2, 3, 5, 7, 8, 10];
    const prog = st.style === 'synth' ? pick([[0, 5, 3, 4], [0, 3, 5, 4], [5, 3, 0, 4]]) : st.style === 'soul' ? pick([[0, 3, 4, 3], [0, 5, 3, 4]]) : pick([[0, 0, 3, 4], [0, 3, 0, 4], [0, 5, 3, 4]]);
    const mel = [];
    for (let i = 0; i < 32; i++) mel.push(Math.random() < (st.style === 'gfunk' ? 0.35 : 0.5) ? pick([0, 2, 4, 7, 9, 11, 12]) : null);
    const bass = [];
    for (let i = 0; i < 16; i++) bass.push(Math.random() < 0.45 || i % 8 === 0 ? pick([0, 0, 7, 12, 10, 5]) : null);
    const kick = [1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0].map((v, i) => v || (Math.random() < 0.1 && i % 2 === 0) ? 1 : 0);
    this.song = { key, minor, prog, mel, bass, kick, swing: st.style === 'gfunk' ? 0.12 : st.style === 'soul' ? 0.16 : 0 };
    this.songBars = 0;
    this.bar = 0; this.step = 0;
  }

  _note(midi) { return 440 * Math.pow(2, (midi - 69) / 12); }

  _schedule(t, stepDur) {
    const st = STATIONS[this.station];
    const S = this.song;
    const s = this.step;
    const out = this.out;
    const A = this.audio;
    const chordDeg = S.prog[this.bar % 4];
    const root = S.key + S.minor[chordDeg % 7];
    const swingT = s % 2 === 1 ? stepDur * S.swing : 0;
    const tt = t + swingT;
    // drums
    if (S.kick[s]) { A._tone(out, tt, { f0: 130, f1: 42, dur: 0.22, vol: 0.9 }); }
    if (s === 4 || s === 12) {
      A._noiseHit(out, tt, { dur: st.style === 'synth' ? 0.25 : 0.16, vol: 0.45, freq: 1800, type: 'bandpass', q: 0.8 });
      A._tone(out, tt, { f0: 220, f1: 150, dur: 0.08, vol: 0.25, type: 'triangle' });
      if (st.style !== 'gfunk') A._noiseHit(out, tt, { dur: 0.4, vol: 0.15, freq: 5000, type: 'highpass' });
    }
    if (st.style === 'synth' ? true : s % 2 === 0) A._noiseHit(out, tt, { dur: 0.03, vol: s % 4 === 2 ? 0.12 : 0.07, freq: 8000, type: 'highpass' });
    if (st.style === 'gfunk' && s === 14 && Math.random() < 0.5) A._noiseHit(out, tt, { dur: 0.15, vol: 0.1, freq: 7000, type: 'highpass' });
    // bass
    const b = S.bass[s];
    if (b != null) {
      const f = this._note(root + b - 12);
      const type = st.style === 'synth' ? 'sawtooth' : 'sine';
      A._tone(out, tt, { f0: f * (st.style === 'gfunk' ? 1.03 : 1), f1: f, dur: stepDur * (st.style === 'soul' ? 3 : 1.8), vol: st.style === 'synth' ? 0.18 : 0.5, type });
    }
    // chords / pads
    if (s === 0 || (st.style === 'soul' && s === 8)) {
      for (const iv of [0, 3, 7, 10]) A._tone(out, tt, { f0: this._note(root + 12 + iv), dur: stepDur * 14, vol: st.style === 'synth' ? 0.045 : 0.035, type: st.style === 'synth' ? 'sawtooth' : 'triangle', attack: 0.08 });
    }
    if (st.style === 'synth' && s % 2 === 0) {
      const arp = [0, 7, 12, 15, 19, 15, 12, 7];
      A._tone(out, tt, { f0: this._note(root + 24 + arp[(s / 2) % 8]), dur: stepDur * 0.9, vol: 0.05, type: 'square' });
    }
    // lead melody (every other 2 bars)
    const mi = (this.bar % 2) * 16 + s;
    const m = S.mel[mi];
    if (m != null && (this.bar % 8) >= 2) {
      const f = this._note(root + 24 + m);
      if (st.style === 'gfunk') {
        // portamento "whistle" lead
        A._tone(out, tt, { f0: f * 0.94, f1: f, dur: stepDur * 1.8, vol: 0.06, type: 'sine', attack: 0.03 });
      } else if (st.style === 'soul') {
        A._tone(out, tt, { f0: f, dur: stepDur * 2.5, vol: 0.06, type: 'triangle', attack: 0.02 });
      } else {
        A._tone(out, tt, { f0: f, dur: stepDur * 1.5, vol: 0.05, type: 'sawtooth', attack: 0.01 });
      }
    }
  }

  update(dt, inVehicle) {
    const ctx = this.ctx;
    const st = STATIONS[this.station];
    const on = inVehicle && st.style !== 'off';
    this.out.gain.setTargetAtTime(on ? 0.6 : 0, ctx.currentTime, 0.3);
    if (!on) { this.nextTime = ctx.currentTime + 0.05; return; }
    const stepDur = 60 / st.bpm / 4;
    if (this.nextTime < ctx.currentTime - 0.2) this.nextTime = ctx.currentTime + 0.05;
    while (this.nextTime < ctx.currentTime + 0.15) {
      this._schedule(this.nextTime, stepDur);
      this.nextTime += stepDur;
      this.step++;
      if (this.step >= 16) {
        this.step = 0; this.bar++; this.songBars++;
        if (this.songBars >= 48) this._newSong();
      }
    }
  }
}

export { STATIONS };
