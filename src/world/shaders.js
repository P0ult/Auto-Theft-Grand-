// GLSL snippets for city surfaces (injected into MeshStandardMaterial via patch()).
import { XS, ZS, HALF_ROAD, SIDEWALK_W } from './citymap.js';

export const NOISE_GLSL = /* glsl */`
float h21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vn2(vec2 p) {
  vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), u.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm3(vec2 p) { return vn2(p) * 0.5 + vn2(p * 2.1 + 3.1) * 0.3 + vn2(p * 4.3 + 7.7) * 0.2; }
`;

const f = (v) => (Number.isInteger(v) ? v.toFixed(1) : String(v));

// ------------------------------------------------------------------ ROADS
export const ROAD_EXT = {
  key: 'road',
  fragPars: NOISE_GLSL + `
const float XS_[${XS.length}] = float[](${XS.map(f).join(',')});
const float ZS_[${ZS.length}] = float[](${ZS.map(f).join(',')});
const float HR = ${f(HALF_ROAD)};
float atgRough = 0.9;
vec3 atgEmit = vec3(0.0);
float stripe(float x, float c, float w, float aa) { return 1.0 - smoothstep(w - aa, w + aa, abs(x - c)); }
`,
  fragColor: `
{
  vec2 wp = vAtgWorld.xz;
  float dxm = 1e5, dzm = 1e5;
  for (int i = 0; i < ${XS.length}; i++) { float d = wp.x - XS_[i]; if (abs(d) < abs(dxm)) dxm = d; }
  for (int j = 0; j < ${ZS.length}; j++) { float d = wp.y - ZS_[j]; if (abs(d) < abs(dzm)) dzm = d; }
  float ax = abs(dxm), az = abs(dzm);
  float n = fbm3(wp * 0.25);
  float fine = vn2(wp * 6.0);
  vec3 col = vec3(0.085, 0.085, 0.09) * (0.72 + 0.45 * n) * (0.9 + 0.2 * fine);
  float aa = max(fwidth(wp.x), fwidth(wp.y)) * 0.8 + 0.01;
  float paintY = 0.0, paintW = 0.0;
  bool inNS = ax < HR, inEW = az < HR;
  float lat = 0.0, lon = 0.0, fromInt = 0.0, side = 0.0;
  if (inEW && !inNS) { lat = dzm; lon = wp.x; fromInt = ax - HR; side = -lat * dxm; }
  else if (inNS && !inEW) { lat = dxm; lon = wp.y; fromInt = az - HR; side = lat * dzm; }
  if ((inEW && !inNS) || (inNS && !inEW)) {
    float wear = smoothstep(0.25, 0.7, vn2(wp * 1.3 + 5.0)) * 0.55 + 0.45;
    // tire-darkened lanes
    float track = stripe(abs(lat), 1.9 - 0.8, 0.35, 0.4) + stripe(abs(lat), 1.9 + 0.8, 0.35, 0.4) + stripe(abs(lat), 5.4 - 0.8, 0.35, 0.4) + stripe(abs(lat), 5.4 + 0.8, 0.35, 0.4);
    col *= 1.0 - track * 0.12;
    // double yellow center
    paintY += (stripe(lat, 0.16, 0.07, aa) + stripe(lat, -0.16, 0.07, aa)) * step(0.0, fromInt);
    // dashed lane dividers
    float dash = step(0.45, fract(lon / 7.0)) * step(4.0, fromInt);
    paintW += stripe(abs(lat), 3.65, 0.07, aa) * dash;
    // parking lane edge
    paintW += stripe(abs(lat), 7.25, 0.06, aa) * step(6.0, fromInt) * 0.8;
    // crosswalk zebra
    float cw = step(0.6, fromInt) * (1.0 - step(4.4, fromInt)) * step(abs(lat), HR - 0.6);
    paintW += cw * step(0.5, fract(lat / 1.2 + 0.25));
    // stop line
    paintW += step(4.9, fromInt) * (1.0 - step(5.5, fromInt)) * step(0.0, side) * step(0.3, abs(lat)) * step(abs(lat), 7.1);
    paintW *= wear; paintY *= wear;
    // oil stains in lane centers near stop lines
    float oil = smoothstep(0.55, 0.9, vn2(wp * 0.9)) * (1.0 - smoothstep(6.0, 22.0, fromInt));
    col *= 1.0 - oil * 0.35;
  } else if (inNS && inEW) {
    // manhole in the middle of intersections
    float mh = 1.0 - smoothstep(0.55, 0.6, length(vec2(dxm, dzm) - vec2(2.5, -3.0)));
    col = mix(col, vec3(0.06), mh);
    col *= 0.95 + 0.1 * vn2(wp * 3.0);
  }
  // cracks / patches
  float patchN = smoothstep(0.62, 0.66, vn2(wp * 0.08 + 13.0));
  col = mix(col, col * 0.75, patchN);
  float crack = smoothstep(0.02, 0.0, abs(vn2(wp * 0.7) - 0.5)) * 0.6;
  col *= 1.0 - crack * 0.5;
  col = mix(col, vec3(0.75, 0.62, 0.18), clamp(paintY, 0.0, 1.0));
  col = mix(col, vec3(0.78, 0.78, 0.76), clamp(paintW, 0.0, 1.0));
  float paint = clamp(paintY + paintW, 0.0, 1.0);
  // wet puddles
  float puddle = smoothstep(0.45, 0.6, fbm3(wp * 0.12 + 2.0)) * uWet;
  col *= 1.0 - uWet * 0.35 - puddle * 0.25;
  atgRough = mix(mix(0.92, 0.6, paint), 0.12, clamp(uWet * 0.55 + puddle, 0.0, 1.0));
  diffuseColor.rgb = col;
}
`,
  fragRoughness: 'roughnessFactor = atgRough;',
};

// ------------------------------------------------------------------ BLOCK TOPS / LOT SURFACES
// aLot.x = surface type, aRect = block rect (x0,z0,x1,z1) for sidewalk distance (or 0 for lots)
export const GROUND_EXT = {
  key: 'ground',
  vertexPars: 'attribute vec4 aRect; attribute float aLot; varying vec4 vRect; varying float vLot; varying vec3 vGN;',
  vertexMain: 'vRect = aRect; vLot = aLot; vGN = normal;',
  fragPars: NOISE_GLSL + `
varying vec4 vRect; varying float vLot; varying vec3 vGN;
float atgRough = 0.9;
const float SW = ${f(SIDEWALK_W)};
`,
  fragColor: `
{
  vec2 wp = vAtgWorld.xz;
  float type = floor(vLot + 0.5);
  float n = fbm3(wp * 0.3);
  vec3 col = vec3(0.5);
  float edge = 1e5;
  if (vRect.z > vRect.x) {
    edge = min(min(wp.x - vRect.x, vRect.z - wp.x), min(wp.y - vRect.y, vRect.w - wp.y));
  }
  if (edge < SW) {
    // sidewalk: concrete slabs with joints; curb band at the edge
    vec2 g = fract(wp / 1.6);
    float joint = step(g.x, 0.025) + step(g.y, 0.025);
    col = vec3(0.54, 0.53, 0.5) * (0.85 + 0.25 * n) * (1.0 - clamp(joint, 0.0, 1.0) * 0.25);
    col = mix(col, vec3(0.66, 0.65, 0.62), step(edge, 0.3));
    col *= 1.0 - smoothstep(0.6, 0.85, vn2(wp * 0.6)) * 0.2; // stains
    atgRough = 0.85;
  } else if (type < 0.5) { // concrete
    col = vec3(0.5, 0.49, 0.47) * (0.8 + 0.3 * n); atgRough = 0.85;
  } else if (type < 1.5) { // grass
    float dry = smoothstep(0.35, 0.75, fbm3(wp * 0.05));
    vec3 g1 = vec3(0.12, 0.2, 0.06), g2 = vec3(0.3, 0.27, 0.12);
    col = mix(g1, g2, dry) * (0.75 + 0.5 * vn2(wp * 2.5)) * (0.85 + 0.3 * n);
    atgRough = 0.95;
  } else if (type < 2.5) { // dirt
    col = vec3(0.32, 0.25, 0.17) * (0.7 + 0.5 * n) * (0.9 + 0.2 * vn2(wp * 5.0)); atgRough = 1.0;
  } else if (type < 3.5) { // asphalt lot with parking lines
    col = vec3(0.1, 0.1, 0.105) * (0.75 + 0.4 * n);
    float pl = step(fract(wp.x / 3.0), 0.04) * step(fract(wp.y / 12.0), 0.45);
    col = mix(col, vec3(0.7), pl * 0.7);
    atgRough = mix(0.9, 0.15, uWet * 0.6);
  } else if (type < 4.5) { // plaza tiles
    vec2 c = floor(wp / 2.0);
    float chk = mod(c.x + c.y, 2.0);
    col = mix(vec3(0.62, 0.58, 0.52), vec3(0.5, 0.46, 0.42), chk) * (0.9 + 0.15 * n);
    vec2 g = fract(wp / 2.0);
    col *= 1.0 - (step(g.x, 0.02) + step(g.y, 0.02)) * 0.2;
    atgRough = mix(0.7, 0.2, uWet);
  } else if (type < 5.5) { // driveway concrete
    col = vec3(0.6, 0.59, 0.56) * (0.85 + 0.2 * n); atgRough = 0.8;
  } else if (type < 6.5) { // basketball court
    col = vec3(0.16, 0.3, 0.42) * (0.9 + 0.15 * n);
    atgRough = 0.6;
  } else if (type < 7.5) { // gravel path
    col = vec3(0.55, 0.47, 0.36) * (0.8 + 0.4 * vn2(wp * 8.0)); atgRough = 1.0;
  } else { // sand
    float rip = sin(wp.x * 1.3 + vn2(wp * 0.2) * 6.0) * 0.5 + 0.5;
    col = vec3(0.76, 0.66, 0.48) * (0.85 + 0.1 * rip + 0.1 * n); atgRough = 1.0;
  }
  if (vGN.y < 0.5) { col = vec3(0.6, 0.59, 0.56) * (0.85 + 0.2 * n); atgRough = 0.8; } // curb faces
  col *= 1.0 - uWet * 0.3;
  diffuseColor.rgb = col;
}
`,
  fragRoughness: 'roughnessFactor = atgRough;',
};

// ------------------------------------------------------------------ BUILDINGS
export const BUILDING_EXT = {
  key: 'building',
  vertexPars: 'attribute vec4 aB; varying vec4 vB; varying vec2 vFac; varying vec3 vWN;',
  vertexMain: 'vB = aB; vFac = uv; vWN = normal;',
  fragPars: NOISE_GLSL + `
varying vec4 vB; varying vec2 vFac; varying vec3 vWN;
float atgRough = 0.85; float atgMetal = 0.0; vec3 atgEmit = vec3(0.0);
float rectM(vec2 f, vec2 lo, vec2 hi, float aa) {
  vec2 a = smoothstep(lo - aa, lo + aa, f) * (1.0 - smoothstep(hi - aa, hi + aa, f));
  return a.x * a.y;
}
vec3 roomInterior(vec2 f, vec3 V, vec3 N, float rnd, float lit, out float backDepth) {
  vec3 T = normalize(cross(vec3(0.0, 1.0, 0.0), N));
  vec3 d = vec3(dot(V, T), V.y, dot(V, -N));
  d.z = max(d.z, 0.05);
  float tx = d.x > 0.0 ? (1.0 - f.x) / max(d.x, 1e-4) : f.x / max(-d.x, 1e-4);
  float ty = d.y > 0.0 ? (1.0 - f.y) / max(d.y, 1e-4) : f.y / max(-d.y, 1e-4);
  float tz = 0.9 / d.z;
  float t = min(min(tx, ty), tz);
  vec3 h = vec3(f, 0.0) + d * t;
  vec3 wallC = mix(vec3(0.62, 0.55, 0.45), vec3(0.45, 0.5, 0.56), rnd);
  wallC = mix(wallC, vec3(0.7, 0.4, 0.35), step(0.8, fract(rnd * 7.3)));
  vec3 c;
  if (t == tz) {
    c = wallC;
    // furniture silhouettes / picture
    c *= 1.0 - 0.55 * step(h.y, 0.32) * step(0.15 + rnd * 0.2, h.x) * step(h.x, 0.6 + rnd * 0.3);
    c = mix(c, vec3(0.2, 0.3, 0.5), step(0.55, h.y) * step(h.y, 0.75) * step(0.35, h.x) * step(h.x, 0.6) * step(0.5, rnd));
  } else if (t == ty) {
    c = d.y > 0.0 ? vec3(0.9, 0.88, 0.84) : vec3(0.32, 0.24, 0.17) * (0.8 + 0.4 * step(0.5, fract(h.x * 4.0)));
  } else {
    c = wallC * 0.78;
  }
  backDepth = h.z;
  // light falloff from ceiling
  c *= mix(0.55, 1.0, h.y) * (lit > 0.5 ? 1.0 : 0.35);
  return c;
}
`,
  fragColor: `
{
  float style = floor(vB.x + 0.5);
  float seed = vB.y;
  float roof = vB.z;
  vec3 tint = diffuseColor.rgb;
  vec3 wpos = vAtgWorld;
  vec3 V = normalize(wpos - cameraPosition);
  if (roof > 1.5) {
    // clay tile roof
    float row = fract(vFac.y * 3.2);
    float colT = fract(vFac.x * 3.0 + step(0.5, fract(vFac.y * 1.6)) * 0.5);
    vec3 c = mix(vec3(0.55, 0.22, 0.12), vec3(0.42, 0.3, 0.26), step(0.6, fract(seed * 13.0)));
    c *= 0.75 + 0.35 * smoothstep(0.0, 0.9, row) * (0.8 + 0.2 * smoothstep(0.0, 0.3, colT) * smoothstep(1.0, 0.7, colT));
    c *= 0.85 + 0.25 * vn2(wpos.xz * 1.7);
    diffuseColor.rgb = c; atgRough = 0.75;
  } else if (roof > 0.5) {
    float n = fbm3(wpos.xz * 0.9);
    vec3 c = mix(vec3(0.34, 0.33, 0.32), vec3(0.42, 0.4, 0.37), step(0.5, seed)) * (0.75 + 0.4 * n);
    float edge = 0.0;
    diffuseColor.rgb = c * (1.0 - uWet * 0.3); atgRough = mix(0.95, 0.4, uWet);
  } else {
    vec2 uv = vFac;
    vec2 cell = floor(uv);
    vec2 f = fract(uv);
    float rnd = h21(cell + seed * 137.1);
    float rnd2 = h21(cell.yx * 1.7 + seed * 71.3);
    vec2 fw2 = fwidth(uv);
    float fw = max(fw2.x, fw2.y);
    float aa = clamp(fw * 0.75, 0.002, 0.5);
    float farK = smoothstep(0.35, 0.95, fw);
    bool ground = vB.w > 0.5 && cell.y < 0.5;
    vec3 wall = tint;
    float win = 0.0;
    vec3 glass = vec3(0.05, 0.07, 0.09);
    float litProb = 0.35;
    vec3 lightCol = mix(vec3(1.0, 0.72, 0.42), vec3(0.85, 0.92, 1.0), step(0.7, rnd2));
    float frame = 0.0;
    float shopSign = 0.0;
    float glassMetal = 0.55;
    if (style < 0.5) { // office concrete ribbons
      wall = tint * vec3(0.62, 0.61, 0.6) * (0.9 + 0.1 * vn2(uv * 3.0));
      win = rectM(vec2(f.x, f.y), vec2(-0.1, 0.3), vec2(1.1, 0.88), aa);
      win *= 1.0 - (1.0 - smoothstep(0.0, aa * 2.0 + 0.015, abs(f.x - 0.5) - 0.47)) * 0.0;
      frame = (1.0 - smoothstep(0.012, 0.012 + aa, min(f.x, 1.0 - f.x))) * win;
      glass = vec3(0.07, 0.1, 0.13); litProb = 0.45; lightCol = mix(lightCol, vec3(0.9, 0.95, 1.0), 0.6);
    } else if (style < 1.5) { // glass curtain wall
      wall = tint * 0.35;
      win = rectM(f, vec2(0.025, 0.2), vec2(0.975, 0.985), aa);
      glass = tint * vec3(0.12, 0.16, 0.2);
      glassMetal = 0.85;
      litProb = 0.5; lightCol = vec3(0.85, 0.93, 1.0);
    } else if (style < 2.5) { // brick with punched windows
      vec2 b = vec2(uv.x * 12.8 + step(0.5, fract(uv.y * 22.5)) * 0.5, uv.y * 45.0);
      vec2 bf = fract(b);
      float mortar = max(1.0 - smoothstep(0.0, 0.08 + fw * 12.0, bf.y), 1.0 - smoothstep(0.0, 0.04 + fw * 12.0, bf.x));
      mortar *= 1.0 - farK;
      vec3 brick = tint * vec3(0.58, 0.3, 0.22) * (0.85 + 0.3 * h21(floor(b)));
      wall = mix(brick, vec3(0.55, 0.52, 0.48), mortar * 0.8);
      win = rectM(f, vec2(0.26, 0.24), vec2(0.74, 0.8), aa);
      frame = rectM(f, vec2(0.22, 0.2), vec2(0.78, 0.84), aa) - win;
      litProb = 0.38;
    } else if (style < 3.5) { // stucco
      wall = tint * vec3(0.82, 0.78, 0.7) * (0.9 + 0.15 * fbm3(uv * 2.0));
      float has = step(0.22, rnd);
      win = rectM(f, vec2(0.3, 0.34), vec2(0.7, 0.8), aa) * has;
      frame = (rectM(f, vec2(0.26, 0.3), vec2(0.74, 0.84), aa) * has - win);
      litProb = 0.33;
    } else if (style < 4.5) { // corrugated industrial
      float rib = sin(uv.x * 6.2831 * 14.0);
      wall = tint * vec3(0.6, 0.62, 0.64) * (0.85 + 0.12 * rib * (1.0 - farK)) * (0.85 + 0.25 * vn2(uv * vec2(1.0, 0.5)));
      float hasW = step(0.55, rnd);
      win = rectM(f, vec2(0.1, 0.72), vec2(0.9, 0.9), aa) * hasW;
      litProb = 0.25; lightCol = vec3(0.9, 1.0, 0.9);
      if (ground && rnd2 > 0.55) {
        float door = rectM(f, vec2(0.08, 0.0), vec2(0.92, 0.72), aa);
        wall = mix(wall, vec3(0.45, 0.46, 0.44) * (0.8 + 0.2 * step(0.5, fract(f.y * 18.0))), door);
      }
    } else if (style < 5.5) { // storefront building: upper floors plain stucco/brick
      wall = tint * vec3(0.78, 0.72, 0.64) * (0.9 + 0.15 * fbm3(uv * 2.0));
      win = rectM(f, vec2(0.25, 0.3), vec2(0.75, 0.8), aa) * step(0.15, rnd);
      frame = rectM(f, vec2(0.22, 0.27), vec2(0.78, 0.83), aa) * step(0.15, rnd) - win;
      litProb = 0.3;
    } else if (style < 6.5) { // art deco
      float pil = 1.0 - rectM(f, vec2(0.14, -0.1), vec2(0.86, 1.1), aa);
      wall = mix(tint * vec3(0.75, 0.7, 0.62), tint * vec3(0.88, 0.84, 0.76), pil);
      win = rectM(f, vec2(0.22, 0.14), vec2(0.78, 0.88), aa);
      frame = step(abs(f.x - 0.5), 0.015) * win;
      litProb = 0.42;
    } else { // modern hotel / mansion: balconies & sliding glass
      wall = tint * vec3(0.9, 0.9, 0.88);
      win = rectM(f, vec2(0.08, 0.12), vec2(0.92, 0.86), aa);
      float rail = step(fract(f.x * 14.0), 0.18) * step(f.y, 0.4) * step(0.12, f.y) + step(abs(f.y - 0.4), 0.015);
      frame = clamp(rail * win, 0.0, 1.0);
      glass = vec3(0.08, 0.12, 0.14);
      litProb = 0.3;
    }
    // Ground floor overrides: shop fronts & lobbies
    if (ground) {
      float isShop = (style > 4.5 && style < 5.5) ? 1.0 : step(0.45, rnd2);
      if ((style > 3.5 && style < 4.5) || vB.w > 1.5) isShop = 0.0;
      if (isShop > 0.5) {
        win = rectM(f, vec2(0.04, 0.05), vec2(0.96, 0.72), aa);
        frame = rectM(f, vec2(0.02, 0.03), vec2(0.98, 0.74), aa) - win;
        shopSign = rectM(f, vec2(-0.1, 0.78), vec2(1.1, 0.97), aa);
        litProb = 0.85;
        lightCol = vec3(1.0, 0.85, 0.6);
      } else if (rnd < 0.18 && style > 1.5) {
        // door
        float door = rectM(f, vec2(0.32, 0.0), vec2(0.68, 0.72), aa);
        wall = mix(wall, vec3(0.2, 0.13, 0.08), door);
        win *= 1.0 - door;
      }
    }
    // floor slab line
    float slab = (1.0 - smoothstep(0.0, 0.02 + aa, f.y)) * (1.0 - farK) * step(style, 2.5) * step(0.5, style);
    wall *= 1.0 - slab * 0.25;
    // distance antialiasing: average the pattern
    float cover = 0.4;
    win = mix(win, cover, farK);
    frame = mix(frame, 0.0, farK);
    // lighting state per cell (per window)
    float lit = step(h21(cell * 1.31 + seed * 19.7 + 0.5), litProb * (0.25 + 0.4 * uNight));
    float backDepth;
    vec3 interior = roomInterior(f, V, normalize(vWN), rnd, lit, backDepth);
    interior = mix(interior, vec3(0.5), farK);
    // compose
    vec3 winCol = glass + interior * 0.18;
    vec3 col = mix(wall, winCol, win);
    col = mix(col, vec3(0.85, 0.84, 0.8) * (style < 2.5 && style > 1.5 ? 0.9 : 1.0), clamp(frame, 0.0, 1.0));
    // shop sign band
    vec3 signCol = 0.5 + 0.5 * cos(6.2831 * (h21(vec2(seed * 91.0, cell.y)) + vec3(0.0, 0.33, 0.67)));
    float glyph = step(0.45, h21(floor(vec2(uv.x * 7.0, f.y * 5.0)) + seed * 3.0)) * rectM(f, vec2(-0.1, 0.81), vec2(1.1, 0.94), aa);
    col = mix(col, signCol * 0.4 + glyph * 0.2, shopSign);
    diffuseColor.rgb = col;
    atgRough = mix(0.88, 0.06, win);
    atgMetal = mix(0.0, glassMetal, win);
    // emission at night (and dimly by day for shops)
    float nightK = smoothstep(0.1, 0.7, uNight);
    vec3 e = interior * lightCol * lit * win * (nightK * 1.25 + (shopSign > 0.0 || litProb > 0.8 ? 0.08 : 0.0));
    e += signCol * (0.25 + glyph * 1.4) * shopSign * (nightK * 3.5 + 0.05);
    // tv flicker
    e *= 1.0 + step(0.93, rnd2) * 0.4 * sin(uTime * 13.0 + rnd * 40.0) * nightK;
    atgEmit = e;
  }
}
`,
  fragRoughness: 'roughnessFactor = atgRough;',
  fragMetal: 'metalnessFactor = atgMetal;',
  fragEmissive: 'totalEmissiveRadiance += atgEmit;',
};

// ------------------------------------------------------------------ PROPS (glowing bulbs & traffic signals)
export const PROP_EXT = {
  key: 'prop',
  vertexPars: 'attribute vec3 aGlow; attribute float aSig; varying vec3 vGlow; varying float vSig; varying float vPhase;\n#ifdef USE_INSTANCING\nattribute float aPhase;\n#endif',
  vertexMain: 'vGlow = aGlow; vSig = aSig;\n#ifdef USE_INSTANCING\nvPhase = aPhase;\n#else\nvPhase = 0.0;\n#endif',
  fragPars: `varying vec3 vGlow; varying float vSig; varying float vPhase;
vec3 atgEmit = vec3(0.0);
// shared with traffic AI: cycle 34s; NS green 0-13, yellow 13-16, red 16-34; EW green 17-30, yellow 30-33
float sigOn(float code, float t) {
  float c = mod(t, 34.0);
  bool nsG = c < 13.0, nsY = c >= 13.0 && c < 16.0;
  bool ewG = c >= 17.0 && c < 30.0, ewY = c >= 30.0 && c < 33.0;
  if (code < 1.5) return (!nsG && !nsY) ? 1.0 : 0.0;
  if (code < 2.5) return nsY ? 1.0 : 0.0;
  if (code < 3.5) return nsG ? 1.0 : 0.0;
  if (code < 4.5) return (!ewG && !ewY) ? 1.0 : 0.0;
  if (code < 5.5) return ewY ? 1.0 : 0.0;
  return ewG ? 1.0 : 0.0;
}
`,
  fragEmissive: `
  totalEmissiveRadiance += vGlow * (0.02 + uStreetLights * 1.0);
  if (vSig > 0.5) {
    float on = sigOn(vSig, uTime + vPhase);
    totalEmissiveRadiance += diffuseColor.rgb * on * 6.0;
    diffuseColor.rgb *= 0.15 + on * 0.2;
  }
`,
  fragEnd: '',
};

// Traffic light phase for an intersection (JS mirror of sigOn)
export function intersectionPhase(i, j) {
  return ((i * 7 + j * 13) % 17) * 2.0;
}
export function signalState(t, axis /* 0 = NS, 1 = EW */) {
  const c = ((t % 34) + 34) % 34;
  if (axis === 0) return c < 13 ? 'green' : c < 16 ? 'yellow' : 'red';
  return c >= 17 && c < 30 ? 'green' : c >= 30 && c < 33 ? 'yellow' : 'red';
}
