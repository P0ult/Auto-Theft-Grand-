// HDR post-processing: MSAA scene target -> screen-space ray-traced reflections & ambient occlusion (from the
// depth buffer) -> bloom mip chain -> god rays -> filmic composite.
import * as THREE from 'three';

const FS_VERT = /* glsl */`
varying vec2 vUv;
void main() { vUv = position.xy * 0.5 + 0.5; gl_Position = vec4(position.xy, 0.0, 1.0); }
`;

function fsMaterial(fragmentShader, uniforms, extra = {}) {
  return new THREE.ShaderMaterial({ vertexShader: FS_VERT, fragmentShader, uniforms, depthTest: false, depthWrite: false, ...extra });
}

const PREFILTER = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform sampler2D tSrc;
uniform vec2 uTexel;
uniform float uThreshold;
uniform float uKnee;
vec3 samp(vec2 uv) { return texture2D(tSrc, uv).rgb; }
void main() {
  // 4-tap box downsample with Karis average to avoid fireflies
  vec3 a = samp(vUv + uTexel * vec2(-1.0, -1.0));
  vec3 b = samp(vUv + uTexel * vec2( 1.0, -1.0));
  vec3 c = samp(vUv + uTexel * vec2(-1.0,  1.0));
  vec3 d = samp(vUv + uTexel * vec2( 1.0,  1.0));
  float wa = 1.0 / (1.0 + max(a.r, max(a.g, a.b)));
  float wb = 1.0 / (1.0 + max(b.r, max(b.g, b.b)));
  float wc = 1.0 / (1.0 + max(c.r, max(c.g, c.b)));
  float wd = 1.0 / (1.0 + max(d.r, max(d.g, d.b)));
  vec3 col = (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
  col = min(col, vec3(60.0));
  float br = max(col.r, max(col.g, col.b));
  float rq = clamp(br - uThreshold + uKnee, 0.0, 2.0 * uKnee);
  rq = (rq * rq) / (4.0 * uKnee + 1e-4);
  float w = max(rq, br - uThreshold) / max(br, 1e-4);
  gl_FragColor = vec4(col * w, 1.0);
}
`;

const DOWN = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform sampler2D tSrc;
uniform vec2 uTexel;
void main() {
  vec2 t = uTexel;
  vec3 A = texture2D(tSrc, vUv + t * vec2(-2.0, -2.0)).rgb;
  vec3 B = texture2D(tSrc, vUv + t * vec2( 0.0, -2.0)).rgb;
  vec3 C = texture2D(tSrc, vUv + t * vec2( 2.0, -2.0)).rgb;
  vec3 D = texture2D(tSrc, vUv + t * vec2(-1.0, -1.0)).rgb;
  vec3 E = texture2D(tSrc, vUv + t * vec2( 1.0, -1.0)).rgb;
  vec3 F = texture2D(tSrc, vUv + t * vec2(-2.0,  0.0)).rgb;
  vec3 G = texture2D(tSrc, vUv).rgb;
  vec3 H = texture2D(tSrc, vUv + t * vec2( 2.0,  0.0)).rgb;
  vec3 I = texture2D(tSrc, vUv + t * vec2(-1.0,  1.0)).rgb;
  vec3 J = texture2D(tSrc, vUv + t * vec2( 1.0,  1.0)).rgb;
  vec3 K = texture2D(tSrc, vUv + t * vec2(-2.0,  2.0)).rgb;
  vec3 L = texture2D(tSrc, vUv + t * vec2( 0.0,  2.0)).rgb;
  vec3 M = texture2D(tSrc, vUv + t * vec2( 2.0,  2.0)).rgb;
  vec3 col = (D + E + I + J) * 0.125 + (A + B + G + F) * 0.03125 + (B + C + H + G) * 0.03125 + (F + G + L + K) * 0.03125 + (G + H + M + L) * 0.03125;
  gl_FragColor = vec4(col, 1.0);
}
`;

const UP = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform sampler2D tSrc;
uniform sampler2D tPrev;
uniform vec2 uTexel;
uniform float uRadius;
void main() {
  vec2 t = uTexel * uRadius;
  vec3 s = texture2D(tSrc, vUv + vec2(-t.x, -t.y)).rgb;
  s += texture2D(tSrc, vUv + vec2(0.0, -t.y)).rgb * 2.0;
  s += texture2D(tSrc, vUv + vec2(t.x, -t.y)).rgb;
  s += texture2D(tSrc, vUv + vec2(-t.x, 0.0)).rgb * 2.0;
  s += texture2D(tSrc, vUv).rgb * 4.0;
  s += texture2D(tSrc, vUv + vec2(t.x, 0.0)).rgb * 2.0;
  s += texture2D(tSrc, vUv + vec2(-t.x, t.y)).rgb;
  s += texture2D(tSrc, vUv + vec2(0.0, t.y)).rgb * 2.0;
  s += texture2D(tSrc, vUv + vec2(t.x, t.y)).rgb;
  gl_FragColor = vec4(texture2D(tPrev, vUv).rgb + s / 16.0, 1.0);
}
`;

const RAYS = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform sampler2D tSrc;
uniform vec2 uSunPos;
uniform float uStrength;
void main() {
  vec2 delta = (vUv - uSunPos) * (1.0 / 40.0) * 0.9;
  vec2 uv = vUv;
  float illum = 1.0;
  vec3 acc = vec3(0.0);
  for (int i = 0; i < 40; i++) {
    uv -= delta;
    vec4 s = texture2D(tSrc, clamp(uv, 0.001, 0.999));
    float sky = 1.0 - smoothstep(0.0, 0.45, s.a); // alpha 0 = sky; 0.5..1 = surfaces (reflectivity lives there)
    vec3 c = min(s.rgb, vec3(4.0)) * sky;
    acc += c * illum;
    illum *= 0.965;
  }
  gl_FragColor = vec4(acc / 40.0 * uStrength, 1.0);
}
`;

// ---------------------------------------------------------------- depth helpers shared by SSR & AO
const DEPTH_GLSL = /* glsl */`
uniform sampler2D tDepth;
uniform mat4 uProj, uProjInv, uView, uViewInv;
uniform float uReversed;
uniform vec2 uDTexel;
float rawDepth(vec2 uv) { return texture2D(tDepth, uv).r; }
bool isSkyDepth(float d) { return uReversed > 0.5 ? d <= 0.0 : d >= 1.0; }
vec3 viewPosD(vec2 uv, float d) {
  vec4 c = vec4(uv * 2.0 - 1.0, uReversed > 0.5 ? d : d * 2.0 - 1.0, 1.0);
  vec4 v = uProjInv * c;
  return v.xyz / v.w;
}
vec3 viewPos(vec2 uv) { return viewPosD(uv, rawDepth(uv)); }
// surface normal from the depth buffer (the smaller of the two one-sided differences, to keep edges crisp)
vec3 viewNormal(vec2 uv, vec3 P) {
  vec3 px = viewPos(uv + vec2(uDTexel.x, 0.0)), nx = viewPos(uv - vec2(uDTexel.x, 0.0));
  vec3 py = viewPos(uv + vec2(0.0, uDTexel.y)), ny = viewPos(uv - vec2(0.0, uDTexel.y));
  vec3 dx = abs(px.z - P.z) < abs(nx.z - P.z) ? px - P : P - nx;
  vec3 dy = abs(py.z - P.z) < abs(ny.z - P.z) ? py - P : P - ny;
  vec3 N = normalize(cross(dx, dy));
  return dot(N, P) > 0.0 ? -N : N;
}
vec2 project(vec3 p) { vec4 c = uProj * vec4(p, 1.0); return c.xy / c.w * 0.5 + 0.5; }
float ign(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }
`;

// Screen-space ray-traced reflections: march the reflected view ray through the depth buffer and pick up the
// colour it hits. Reflectivity comes from the scene's alpha (materials write 1 - 0.5 * refl); open water is
// recognised by its height. Output is premultiplied: rgb = colour * weight, a = weight.
const SSR = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform sampler2D tScene;
uniform sampler2D tWater;
uniform vec4 uWaterRect;
uniform vec4 uLake;
uniform float uWaterY;
uniform float uTime, uRain, uWet;
uniform float uMaxDist;
uniform int uSteps;
${DEPTH_GLSL}
float h21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
vec2 ripples(vec2 xz) {
  if (uRain < 0.02) return vec2(0.0);
  vec2 g = vec2(0.0);
  for (int k = 0; k < 3; k++) {
    vec2 p = xz * 2.3 + vec2(float(k) * 0.43, float(k) * 0.77);
    vec2 cell = floor(p);
    float h = h21(cell + float(k) * 17.0);
    if (h > uRain * 0.85 + 0.15) continue;
    vec2 c = cell + 0.3 + 0.4 * vec2(h, h21(cell.yx + 3.7 + float(k)));
    float ph = fract(uTime * 0.9 + h * 13.0);
    vec2 dv = p - c; float d = length(dv);
    float x = (d - ph * 0.3) * 40.0;
    g += dv / max(d, 1e-3) * sin(x) * (1.0 - smoothstep(0.0, 3.1416, abs(x))) * (1.0 - ph) * (1.0 - ph);
  }
  return g * 0.4 * clamp(uRain * 1.5, 0.0, 1.0);
}
vec2 waves(vec2 p, float t) {
  vec2 g = vec2(0.6, 0.8) * cos(dot(p, vec2(0.6, 0.8)) * 0.35 + t * 1.3) * 0.35;
  g += vec2(-0.7, 0.7) * cos(dot(p, vec2(-0.7, 0.7)) * 0.62 + t * 1.7) * 0.2;
  g += vec2(0.2, -1.0) * cos(dot(p, vec2(0.2, -1.0)) * 1.3 + t * 2.3) * 0.12;
  return g * 0.35;
}
void main() {
  float d0 = rawDepth(vUv);
  if (isSkyDepth(d0)) { gl_FragColor = vec4(0.0); return; }
  vec4 sc = texture2D(tScene, vUv);
  float refl = clamp((1.0 - sc.a) * 2.0, 0.0, 1.0);
  vec3 P = viewPosD(vUv, d0);
  vec3 W = (uViewInv * vec4(P, 1.0)).xyz;
  // open water: the sea (depth map) and the lake
  vec2 wuv = (W.xz - uWaterRect.xy) / uWaterRect.zw;
  float sea = step(abs(W.y - uWaterY), 0.4) * step(0.02, texture2D(tWater, clamp(wuv, 0.0, 1.0)).r) * step(0.0, wuv.x) * step(wuv.x, 1.0) * step(0.0, wuv.y) * step(wuv.y, 1.0);
  float lake = step(length(W.xz - uLake.xy), uLake.z) * step(abs(W.y - uLake.w), 0.4);
  float water = max(sea, lake);
  refl = max(refl, water * 0.85);
  if (refl < 0.02 || -P.z > uMaxDist * 6.0) { gl_FragColor = vec4(0.0); return; }
  vec3 N = viewNormal(vUv, P);
  vec3 upV = normalize((uView * vec4(0.0, 1.0, 0.0, 0.0)).xyz);
  // level surfaces: use the exact up vector (depth-buffer normals are noisy far away), ruffled by rain / waves
  if (dot(N, upV) > 0.92 || water > 0.5) {
    vec2 s = water > 0.5 ? waves(W.xz, uTime) : ripples(W.xz);
    N = normalize((uView * vec4(normalize(vec3(-s.x, 1.0, -s.y)), 0.0)).xyz);
  }
  vec3 V = normalize(P);
  vec3 R = normalize(reflect(V, N));
  float cosV = max(dot(-V, N), 0.0);
  float fres = 0.04 + 0.96 * pow(1.0 - cosV, 5.0);
  float wgt = refl * clamp(0.3 + 0.7 * fres * 1.4, 0.0, 1.0);
  // march with growing steps, then refine the hit with a binary search
  float jit = ign(gl_FragCoord.xy);
  float prevT = 0.0, hitT = -1.0;
  vec2 huv = vec2(0.0);
  for (int i = 1; i <= 48; i++) {
    if (i > uSteps) break;
    float t = uMaxDist * pow((float(i) - 1.0 + jit) / float(uSteps), 1.8) + 0.15;
    vec3 Q = P + R * t;
    if (Q.z > -0.05) break;
    vec2 uv = project(Q);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
    float sd = rawDepth(uv);
    // the sky (alpha 0) is left to the materials' own environment reflection
    if (isSkyDepth(sd) || texture2D(tScene, uv).a < 0.3) { prevT = t; continue; }
    float sz = viewPosD(uv, sd).z;
    float behind = sz - Q.z; // > 0: the ray has passed behind the surface seen there
    if (behind > 0.0) {
      if (behind < max(0.35, t * 0.12)) {
        float a = prevT, b = t;
        for (int k = 0; k < 5; k++) {
          float m = (a + b) * 0.5;
          vec3 M = P + R * m;
          vec2 mu = project(M);
          if (viewPos(mu).z - M.z > 0.0) b = m; else a = m;
        }
        hitT = b;
        huv = project(P + R * b);
      }
      break;
    }
    prevT = t;
  }
  if (hitT < 0.0) { gl_FragColor = vec4(0.0); return; }
  vec3 col = texture2D(tScene, huv).rgb;
  float edge = smoothstep(0.0, 0.07, min(huv.x, 1.0 - huv.x)) * smoothstep(0.0, 0.07, min(huv.y, 1.0 - huv.y));
  float far = 1.0 - smoothstep(0.55, 1.0, hitT / uMaxDist);
  float fade = edge * far * smoothstep(-0.1, 0.25, -R.z + 0.2);
  float w = clamp(wgt * fade, 0.0, 1.0);
  gl_FragColor = vec4(min(col, vec3(40.0)) * w, w);
}
`;

// Screen-space ambient occlusion: how much nearby geometry hides each point (contact shadows under cars,
// in corners and doorways). Output in .r (1 = open).
const AO = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform float uRadius;
uniform float uStrength;
${DEPTH_GLSL}
void main() {
  float d0 = rawDepth(vUv);
  if (isSkyDepth(d0)) { gl_FragColor = vec4(1.0); return; }
  vec3 P = viewPosD(vUv, d0);
  if (-P.z > 220.0) { gl_FragColor = vec4(1.0); return; }
  vec3 N = viewNormal(vUv, P);
  vec3 T = normalize(abs(N.y) < 0.99 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
  vec3 B = cross(N, T);
  float rot = ign(gl_FragCoord.xy) * 6.2831;
  float r = uRadius * (1.0 + -P.z * 0.012);
  float occ = 0.0;
  for (int i = 0; i < 10; i++) {
    float fi = float(i);
    float a = rot + fi * 2.39996;
    float rr = r * (0.2 + 0.8 * fract(fi * 0.618 + 0.31));
    float h = 0.25 + 0.75 * fract(fi * 0.7548 + 0.17);
    vec3 dir = normalize(T * cos(a) + B * sin(a)) * sqrt(1.0 - h * h) + N * h;
    vec3 S = P + dir * rr;
    vec2 uv = project(S);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
    float sz = viewPos(uv).z;
    float range = smoothstep(0.0, 1.0, rr / max(abs(P.z - sz), 1e-3));
    occ += step(S.z + 0.04 + 0.002 * -P.z, sz) * range;
  }
  float ao = 1.0 - clamp(occ / 10.0 * uStrength, 0.0, 0.85);
  gl_FragColor = vec4(ao, ao, ao, 1.0);
}
`;

const COMPOSITE = /* glsl */`
precision highp float;
varying vec2 vUv;
uniform sampler2D tScene;
uniform sampler2D tBloom;
uniform sampler2D tRays;
uniform float uBloom;
uniform float uExposure;
uniform float uTime;
uniform float uSaturation;
uniform float uContrast;
uniform vec3 uTint;
uniform vec3 uLift;
uniform float uVignette;
uniform float uGrain;
uniform float uDamage;
uniform float uDesat;
uniform float uChroma;
uniform float uFade;
uniform float uRaysOn;
uniform vec2 uResolution;
uniform float uDeath;
uniform float uFlash;
uniform sampler2D tSSR;
uniform sampler2D tAO;
uniform float uSSROn;
uniform float uAOOn;
uniform vec2 uAOTexel;

vec3 ACESFilm(vec3 x) {
  // Stephen Hill fitted ACES
  const mat3 ACESIn = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
  const mat3 ACESOut = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
  x = ACESIn * x;
  vec3 a = x * (x + 0.0245786) - 0.000090537;
  vec3 b = x * (0.983729 * x + 0.4329510) + 0.238081;
  return clamp(ACESOut * (a / b), 0.0, 1.0);
}
vec3 toSRGB(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main() {
  vec2 uv = vUv;
  vec2 cc = uv - 0.5;
  float r2 = dot(cc, cc);
  vec3 col;
  if (uChroma > 0.0) {
    vec2 off = cc * r2 * uChroma;
    col.r = texture2D(tScene, uv + off).r;
    col.g = texture2D(tScene, uv).g;
    col.b = texture2D(tScene, uv - off).b;
  } else {
    col = texture2D(tScene, uv).rgb;
  }
  if (uDeath > 0.0) {
    // death: soft ghosting blur that grows toward the edges
    vec2 px = (0.6 + 2.4 * r2 * 4.0) * uDeath / uResolution * 3.0;
    vec3 acc = col;
    acc += texture2D(tScene, uv + vec2(px.x, 0.0)).rgb + texture2D(tScene, uv - vec2(px.x, 0.0)).rgb;
    acc += texture2D(tScene, uv + vec2(0.0, px.y)).rgb + texture2D(tScene, uv - vec2(0.0, px.y)).rgb;
    acc += texture2D(tScene, uv + px * 0.7).rgb + texture2D(tScene, uv - px * 0.7).rgb;
    col = mix(col, acc / 7.0, clamp(uDeath, 0.0, 1.0) * 0.7);
  }
  if (uAOOn > 0.5) {
    // light blur of the half-res occlusion
    float ao = texture2D(tAO, uv).r * 0.4;
    ao += texture2D(tAO, uv + vec2(uAOTexel.x, 0.0)).r * 0.15 + texture2D(tAO, uv - vec2(uAOTexel.x, 0.0)).r * 0.15;
    ao += texture2D(tAO, uv + vec2(0.0, uAOTexel.y)).r * 0.15 + texture2D(tAO, uv - vec2(0.0, uAOTexel.y)).r * 0.15;
    col *= ao;
  }
  if (uSSROn > 0.5) {
    vec4 rf = texture2D(tSSR, uv);
    col = col * (1.0 - rf.a) + rf.rgb;
  }
  col += texture2D(tBloom, uv).rgb * uBloom;
  if (uRaysOn > 0.5) col += texture2D(tRays, uv).rgb;
  col *= uExposure;
  col *= uTint;
  col = ACESFilm(col);
  // grading in display-ish space
  float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
  col = mix(vec3(l), col, uSaturation * (1.0 - uDesat));
  col = (col - 0.5) * uContrast + 0.5;
  col = col + uLift * (1.0 - col);
  col = clamp(col, 0.0, 1.0);
  if (uDeath > 0.0) {
    // GTA-style death grade: harsh black & white with crushed blacks and a slow darkening
    float g = dot(col, vec3(0.2126, 0.7152, 0.0722));
    g = smoothstep(0.04, 0.92, g);
    g = pow(g, 1.12);
    col = mix(col, vec3(g), clamp(uDeath * 1.3, 0.0, 1.0));
    col *= 1.0 - 0.18 * uDeath;
  }
  col = mix(col, vec3(1.0), clamp(uFlash, 0.0, 1.0));
  // vignette
  float vig = smoothstep(0.85, 0.15, r2 * (uVignette + uDeath * 0.9) * 2.2);
  col *= mix(1.0, vig, 0.55 + 0.25 * uDeath);
  // damage vignette
  col = mix(col, vec3(0.55, 0.0, 0.0), uDamage * smoothstep(0.05, 0.45, r2) * 0.8);
  col = toSRGB(col);
  // grain & dithering
  float n = hash(uv * uResolution + fract(uTime) * 100.0) - 0.5;
  col += n * uGrain + (hash(uv * uResolution * 1.3) - 0.5) / 255.0;
  col *= (1.0 - uFade);
  gl_FragColor = vec4(col, 1.0);
}
`;

export class PostFX {
  constructor(renderer) {
    this.renderer = renderer;
    this.enabled = true;
    this.bloomEnabled = true;
    this.raysEnabled = true;
    this.samples = 4;
    this.scale = 1;
    this.quad = new THREE.Mesh(new THREE.BufferGeometry(), null);
    const g = this.quad.geometry;
    g.setAttribute('position', new THREE.Float32BufferAttribute([-1, -1, 0, 3, -1, 0, -1, 3, 0], 3));
    this.quad.frustumCulled = false;
    this.cam = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    this.qscene = new THREE.Scene();
    this.qscene.add(this.quad);

    this.prefilter = fsMaterial(PREFILTER, { tSrc: { value: null }, uTexel: { value: new THREE.Vector2() }, uThreshold: { value: 1.0 }, uKnee: { value: 0.6 } });
    this.down = fsMaterial(DOWN, { tSrc: { value: null }, uTexel: { value: new THREE.Vector2() } });
    this.up = fsMaterial(UP, { tSrc: { value: null }, tPrev: { value: null }, uTexel: { value: new THREE.Vector2() }, uRadius: { value: 1.0 } });
    this.rays = fsMaterial(RAYS, { tSrc: { value: null }, uSunPos: { value: new THREE.Vector2(0.5, 0.5) }, uStrength: { value: 0 } });
    this.composite = fsMaterial(COMPOSITE, {
      tScene: { value: null }, tBloom: { value: null }, tRays: { value: null },
      uBloom: { value: 0.08 }, uExposure: { value: 1.0 }, uTime: { value: 0 },
      uSaturation: { value: 1.12 }, uContrast: { value: 1.06 },
      uTint: { value: new THREE.Color(1.05, 1.0, 0.93) }, uLift: { value: new THREE.Color(0.012, 0.01, 0.018) },
      uVignette: { value: 0.8 }, uGrain: { value: 0.025 }, uDamage: { value: 0 }, uDesat: { value: 0 },
      uChroma: { value: 0.0022 }, uFade: { value: 0 }, uRaysOn: { value: 1 }, uResolution: { value: new THREE.Vector2(1, 1) },
      uDeath: { value: 0 }, uFlash: { value: 0 },
      tSSR: { value: null }, tAO: { value: null }, uSSROn: { value: 0 }, uAOOn: { value: 0 }, uAOTexel: { value: new THREE.Vector2(1, 1) },
    });
    // depth-buffer effects
    const depthU = () => ({
      tDepth: { value: null }, uProj: { value: new THREE.Matrix4() }, uProjInv: { value: new THREE.Matrix4() },
      uView: { value: new THREE.Matrix4() }, uViewInv: { value: new THREE.Matrix4() }, uReversed: { value: 0 }, uDTexel: { value: new THREE.Vector2() },
    });
    this.ssr = fsMaterial(SSR, {
      ...depthU(), tScene: { value: null }, tWater: { value: null }, uWaterRect: { value: new THREE.Vector4(0, 0, 1, 1) },
      uLake: { value: new THREE.Vector4(0, 0, 0, -1e4) }, uWaterY: { value: -1e4 },
      uTime: { value: 0 }, uRain: { value: 0 }, uWet: { value: 0 }, uMaxDist: { value: 90 }, uSteps: { value: 24 },
    });
    this.ao = fsMaterial(AO, { ...depthU(), uRadius: { value: 0.7 }, uStrength: { value: 1.6 } });
    this.ssrEnabled = false;
    this.aoEnabled = false;
    this.ssrScale = 0.5;
    this.blackTex = new THREE.DataTexture(new Uint8Array([0, 0, 0, 255]), 1, 1);
    this.blackTex.needsUpdate = true;
    this.targets = null;
    this.width = 0; this.height = 0;
  }

  // open water the reflection pass should treat as a mirror (the sea's depth map, the lake)
  setWater(depthTex, rect, waterY, lake) {
    const u = this.ssr.uniforms;
    u.tWater.value = depthTex;
    u.uWaterRect.value.copy(rect);
    u.uWaterY.value = waterY;
    if (lake) u.uLake.value.set(lake.x, lake.z, lake.r, lake.y);
  }

  _makeRT(w, h, samples = 0) {
    return new THREE.WebGLRenderTarget(w, h, {
      type: THREE.HalfFloatType, format: THREE.RGBAFormat, samples,
      minFilter: THREE.LinearFilter, magFilter: THREE.LinearFilter, depthBuffer: samples >= 0,
      generateMipmaps: false,
    });
  }

  setSize(w, h) {
    this.width = w; this.height = h;
    if (this.targets) {
      this.targets.scene.dispose(); this.targets.mips.forEach((m) => m.dispose()); this.targets.ups.forEach((m) => m.dispose());
      this.targets.rays.dispose();
      this.targets.ssr.dispose(); this.targets.ao.dispose();
    }
    const sw = Math.max(1, Math.floor(w * this.scale)), sh = Math.max(1, Math.floor(h * this.scale));
    const scene = this._makeRT(sw, sh, this.samples);
    // a 32-bit float depth attachment is what makes the reversed depth buffer precise; the reflection and
    // occlusion passes read it too
    scene.depthTexture = new THREE.DepthTexture(sw, sh, this.renderer.capabilities.reversedDepthBuffer ? THREE.FloatType : THREE.UnsignedIntType);
    const mips = [], ups = [];
    let mw = Math.max(1, sw >> 1), mh = Math.max(1, sh >> 1);
    for (let i = 0; i < 6; i++) {
      const rt = this._makeRT(mw, mh); rt.depthBuffer = false; mips.push(rt);
      const u = this._makeRT(mw, mh); ups.push(u);
      mw = Math.max(1, mw >> 1); mh = Math.max(1, mh >> 1);
    }
    const rays = this._makeRT(Math.max(1, sw >> 2), Math.max(1, sh >> 2));
    const rs = this.ssrScale;
    const ssr = this._makeRT(Math.max(1, Math.round(sw * rs)), Math.max(1, Math.round(sh * rs)));
    const ao = this._makeRT(Math.max(1, sw >> 1), Math.max(1, sh >> 1));
    ssr.depthBuffer = false; ao.depthBuffer = false;
    this.targets = { scene, mips, ups, rays, ssr, ao, sw, sh };
    this.composite.uniforms.uAOTexel.value.set(1 / ao.width, 1 / ao.height);
    this.composite.uniforms.uResolution.value.set(w, h);
  }

  _pass(mat, target) {
    this.quad.material = mat;
    this.renderer.setRenderTarget(target);
    this.renderer.render(this.qscene, this.cam);
  }

  render(scene, camera, sunScreen, sunStrength) {
    const r = this.renderer;
    if (!this.enabled) {
      r.setRenderTarget(null);
      r.toneMapping = THREE.ACESFilmicToneMapping;
      r.render(scene, camera);
      return;
    }
    r.toneMapping = THREE.NoToneMapping;
    const T = this.targets;
    r.setRenderTarget(T.scene);
    r.render(scene, camera);

    // Bloom
    let bloomTex = this.blackTex;
    if (this.bloomEnabled) {
      this.prefilter.uniforms.tSrc.value = T.scene.texture;
      this.prefilter.uniforms.uTexel.value.set(1 / T.sw, 1 / T.sh);
      this._pass(this.prefilter, T.mips[0]);
      for (let i = 1; i < T.mips.length; i++) {
        const src = T.mips[i - 1];
        this.down.uniforms.tSrc.value = src.texture;
        this.down.uniforms.uTexel.value.set(1 / src.width, 1 / src.height);
        this._pass(this.down, T.mips[i]);
      }
      // upsample: ups[n-1] = mips[n-1]; ups[i] = mips[i] + up(ups[i+1])
      const n = T.mips.length;
      let prevTex = T.mips[n - 1].texture;
      for (let i = n - 2; i >= 0; i--) {
        this.up.uniforms.tSrc.value = prevTex;
        this.up.uniforms.tPrev.value = T.mips[i].texture;
        const srcRT = i === n - 2 ? T.mips[n - 1] : T.ups[i + 1];
        this.up.uniforms.uTexel.value.set(1 / srcRT.width, 1 / srcRT.height);
        this._pass(this.up, T.ups[i]);
        prevTex = T.ups[i].texture;
      }
      bloomTex = T.ups[0].texture;
    }

    // God rays
    const raysOn = this.raysEnabled && sunStrength > 0.001 && sunScreen;
    if (raysOn) {
      this.rays.uniforms.tSrc.value = T.scene.texture;
      this.rays.uniforms.uSunPos.value.copy(sunScreen);
      this.rays.uniforms.uStrength.value = sunStrength;
      this._pass(this.rays, T.rays);
    }

    // Reflections & ambient occlusion from the depth buffer
    const ssrOn = this.ssrEnabled, aoOn = this.aoEnabled;
    if (ssrOn || aoOn) {
      const rev = r.capabilities.reversedDepthBuffer ? 1 : 0;
      for (const m of [this.ssr, this.ao]) {
        const u = m.uniforms;
        u.tDepth.value = T.scene.depthTexture;
        u.uProj.value.copy(camera.projectionMatrix);
        u.uProjInv.value.copy(camera.projectionMatrixInverse);
        u.uView.value.copy(camera.matrixWorldInverse);
        u.uViewInv.value.copy(camera.matrixWorld);
        u.uReversed.value = rev;
        u.uDTexel.value.set(1 / T.sw, 1 / T.sh);
      }
      if (ssrOn) {
        const u = this.ssr.uniforms;
        u.tScene.value = T.scene.texture;
        u.uTime.value = this.time || 0;
        this._pass(this.ssr, T.ssr);
      }
      if (aoOn) this._pass(this.ao, T.ao);
    }

    const c = this.composite.uniforms;
    c.tSSR.value = ssrOn ? T.ssr.texture : this.blackTex;
    c.tAO.value = aoOn ? T.ao.texture : this.blackTex;
    c.uSSROn.value = ssrOn ? 1 : 0;
    c.uAOOn.value = aoOn ? 1 : 0;
    c.tScene.value = T.scene.texture;
    c.tBloom.value = bloomTex;
    c.tRays.value = raysOn ? T.rays.texture : this.blackTex;
    c.uRaysOn.value = raysOn ? 1 : 0;
    this._pass(this.composite, null);
  }
}
