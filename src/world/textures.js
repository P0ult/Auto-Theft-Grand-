// Procedurally drawn canvas textures (foliage, ads, signs).
import * as THREE from 'three';
import { RNG } from '../core/utils.js';

function canvas(w, h) {
  const c = document.createElement('canvas');
  c.width = w; c.height = h;
  return [c, c.getContext('2d')];
}

function tex(c, srgb = true, repeat = false) {
  const t = new THREE.CanvasTexture(c);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  if (repeat) t.wrapS = t.wrapT = THREE.RepeatWrapping;
  t.needsUpdate = true;
  return t;
}

export function palmFrondTexture() {
  const [c, g] = canvas(128, 512);
  const r = new RNG(7);
  g.clearRect(0, 0, 128, 512);
  // stem
  g.strokeStyle = '#5b6b2a'; g.lineWidth = 5;
  g.beginPath(); g.moveTo(64, 512); g.lineTo(64, 10); g.stroke();
  // leaflets
  for (let y = 500; y > 20; y -= 7) {
    const t = 1 - y / 512;
    const len = 58 * Math.sin(Math.min(1, (1 - t) * 1.25) * Math.PI * 0.9) + 6;
    for (const s of [-1, 1]) {
      const hue = 80 + r.range(-12, 10), light = 22 + r.range(-4, 10) + t * 6;
      g.strokeStyle = `hsl(${hue},${45 + r.range(-10, 10)}%,${light}%)`;
      g.lineWidth = 3.2;
      g.beginPath();
      g.moveTo(64, y);
      g.quadraticCurveTo(64 + s * len * 0.5, y - 10, 64 + s * len, y - 26 - r.range(0, 8));
      g.stroke();
    }
  }
  return tex(c);
}

export function foliageTexture() {
  const [c, g] = canvas(256, 256);
  const r = new RNG(3);
  g.clearRect(0, 0, 256, 256);
  for (let i = 0; i < 900; i++) {
    const x = r.range(8, 248), y = r.range(8, 248);
    const dx = x - 128, dy = y - 128;
    if (dx * dx + dy * dy > 118 * 118) continue;
    g.fillStyle = `hsl(${95 + r.range(-20, 15)},${40 + r.range(-10, 15)}%,${18 + r.range(0, 22)}%)`;
    g.beginPath();
    g.ellipse(x, y, r.range(4, 9), r.range(2, 5), r.range(0, 6.28), 0, Math.PI * 2);
    g.fill();
  }
  return tex(c);
}

const ADS = [
  { bg: ['#d62828', '#8b0000'], title: 'SOLAR COLA', sub: 'Taste the heat', fg: '#fff' },
  { bg: ['#ffb703', '#fb8500'], title: 'MAXX BURGER', sub: 'Big. Bigger. MAXX.', fg: '#3a0ca3' },
  { bg: ['#023047', '#219ebc'], title: 'VISTA FM 98.1', sub: 'West Coast Classics', fg: '#ffb703' },
  { bg: ['#2b9348', '#007f5f'], title: 'SPEEDY LOANS', sub: 'Cash in 5 minutes*', fg: '#fff' },
  { bg: ['#111', '#444'], title: 'ZENITH MOTORS', sub: 'Drive the future', fg: '#e5e5e5' },
  { bg: ['#f1faee', '#a8dadc'], title: 'DR. SMILE', sub: 'Dental that shines', fg: '#1d3557' },
  { bg: ['#7209b7', '#3a0ca3'], title: 'LOS SOLES LOTTO', sub: 'You could be next!', fg: '#fee440' },
  { bg: ['#e76f51', '#f4a261'], title: 'SANTA LUZ PIER', sub: 'Ride the big wheel', fg: '#fff' },
];

export function adAtlas() {
  const W = 512, H = 256;
  const [c, g] = canvas(W * 2, H * 4);
  ADS.forEach((ad, i) => {
    const x = (i % 2) * W, y = Math.floor(i / 2) * H;
    const grd = g.createLinearGradient(x, y, x + W, y + H);
    grd.addColorStop(0, ad.bg[0]); grd.addColorStop(1, ad.bg[1]);
    g.fillStyle = grd; g.fillRect(x, y, W, H);
    g.fillStyle = 'rgba(255,255,255,0.12)';
    for (let k = 0; k < 6; k++) { g.beginPath(); g.arc(x + 60 + k * 90, y + H - 20, 40 + k * 6, 0, Math.PI * 2); g.fill(); }
    g.fillStyle = ad.fg;
    g.font = 'bold 74px Impact, "Arial Black", sans-serif';
    g.textAlign = 'center'; g.textBaseline = 'middle';
    g.fillText(ad.title, x + W / 2, y + H * 0.42, W - 30);
    g.font = 'italic 34px Georgia, serif';
    g.fillText(ad.sub, x + W / 2, y + H * 0.75, W - 40);
    g.strokeStyle = 'rgba(0,0,0,0.5)'; g.lineWidth = 8; g.strokeRect(x + 4, y + 4, W - 8, H - 8);
  });
  return { texture: tex(c), count: ADS.length, cols: 2, rows: 4 };
}

export function signTexture(text, opts = {}) {
  const { bg = '#111', fg = '#ffe28a', w = 1024, h = 192, glow = true, font = 'bold 130px Impact, "Arial Black", sans-serif' } = opts;
  const [c, g] = canvas(w, h);
  g.fillStyle = bg; g.fillRect(0, 0, w, h);
  g.font = font; g.textAlign = 'center'; g.textBaseline = 'middle';
  if (glow) { g.shadowColor = fg; g.shadowBlur = 24; }
  g.fillStyle = fg;
  g.fillText(text, w / 2, h / 2 + 6, w - 40);
  return tex(c);
}

export function letterTexture(ch) {
  const [c, g] = canvas(256, 256);
  g.clearRect(0, 0, 256, 256);
  g.fillStyle = '#fff';
  g.font = 'bold 250px "Arial Black", Impact, sans-serif';
  g.textAlign = 'center'; g.textBaseline = 'middle';
  g.fillText(ch, 128, 140, 240);
  return tex(c);
}

export function softDotTexture() {
  const [c, g] = canvas(128, 128);
  const grd = g.createRadialGradient(64, 64, 0, 64, 64, 64);
  grd.addColorStop(0, 'rgba(255,255,255,1)');
  grd.addColorStop(0.35, 'rgba(255,255,255,0.45)');
  grd.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = grd; g.fillRect(0, 0, 128, 128);
  return tex(c, false);
}

export function smokeTexture() {
  const [c, g] = canvas(128, 128);
  const r = new RNG(11);
  g.clearRect(0, 0, 128, 128);
  for (let i = 0; i < 30; i++) {
    const x = 64 + r.range(-26, 26), y = 64 + r.range(-26, 26), rad = r.range(18, 40);
    const grd = g.createRadialGradient(x, y, 0, x, y, rad);
    grd.addColorStop(0, 'rgba(255,255,255,0.22)');
    grd.addColorStop(1, 'rgba(255,255,255,0)');
    g.fillStyle = grd; g.fillRect(0, 0, 128, 128);
  }
  return tex(c, false);
}
