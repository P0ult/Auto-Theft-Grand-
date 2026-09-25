// Atmospheric sky: Preetham-style scattering, animated clouds, stars, moon, city glow.
import * as THREE from 'three';

const SKY_COMMON = /* glsl */`
const float PI_S = 3.141592653589793;
const vec3 totalRayleigh = vec3(5.804542996261093E-6, 1.3562911419845635E-5, 3.0265902468824876E-5);
const vec3 MieConst = vec3(1.8399918514433978E14, 2.7798023919660528E14, 4.0790479543861094E14);
const float cutoffAngle = 1.6110731556870734;
const float steepness = 1.5;
const float EE = 1000.0;
const float rayleighZenithLength = 8.4E3;
const float mieZenithLength = 1.25E3;
const float THREE_OVER_SIXTEENPI = 0.05968310365946075;
const float ONE_OVER_FOURPI = 0.07957747154594767;
float sunIntensity(float zenithAngleCos) {
  zenithAngleCos = clamp(zenithAngleCos, -1.0, 1.0);
  return EE * max(0.0, 1.0 - pow(2.718281828, -((cutoffAngle - acos(zenithAngleCos)) / steepness)));
}
vec3 totalMie(float T) { float c = (0.2 * T) * 10E-18; return 0.434 * c * MieConst; }
float rayleighPhase(float cosTheta) { return THREE_OVER_SIXTEENPI * (1.0 + pow(cosTheta, 2.0)); }
float hgPhase(float cosTheta, float g) {
  float g2 = pow(g, 2.0);
  float inv = 1.0 / pow(1.0 - 2.0 * g * cosTheta + g2, 1.5);
  return ONE_OVER_FOURPI * ((1.0 - g2) * inv);
}
float hash13(vec3 p3) { p3 = fract(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm5(vec2 p) {
  float s = 0.0, a = 0.5;
  mat2 r = mat2(0.8, 0.6, -0.6, 0.8);
  for (int i = 0; i < 5; i++) { s += a * vnoise(p); p = r * p * 2.03 + 11.7; a *= 0.5; }
  return s;
}
`;

const vertexShader = /* glsl */`
varying vec3 vDir;
void main() {
  vDir = normalize(position);
  vec4 p = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
  gl_Position = vec4(p.xy, p.w * 0.99999, p.w);
}
`;

const fragmentShader = /* glsl */`
precision highp float;
varying vec3 vDir;
uniform vec3 uSunDir;
uniform vec3 uMoonDir;
uniform vec3 uSunLight;
uniform vec3 uAmbient;
uniform float uTime;
uniform float uCloudCover;
uniform float uCloudDark;
uniform float uNight;
uniform float uExposure;
uniform float uTurbidity;
uniform float uRayleigh;
uniform float uMieCoef;
uniform float uMieG;
${SKY_COMMON}

vec3 scatter(vec3 dir, vec3 sunDir, out vec3 FexOut) {
  vec3 up = vec3(0.0, 1.0, 0.0);
  float vSunE = sunIntensity(dot(sunDir, up));
  float vSunfade = 1.0 - clamp(1.0 - exp(sunDir.y), 0.0, 1.0);
  float rayleighCoefficient = uRayleigh - (1.0 * (1.0 - vSunfade));
  vec3 vBetaR = totalRayleigh * rayleighCoefficient;
  vec3 vBetaM = totalMie(uTurbidity) * uMieCoef;
  float zenithAngle = acos(max(0.0, dot(up, dir)));
  float inv = 1.0 / (cos(zenithAngle) + 0.15 * pow(93.885 - ((zenithAngle * 180.0) / PI_S), -1.253));
  float sR = rayleighZenithLength * inv;
  float sM = mieZenithLength * inv;
  vec3 Fex = exp(-(vBetaR * sR + vBetaM * sM));
  FexOut = Fex;
  float cosTheta = dot(dir, sunDir);
  float rPhase = rayleighPhase(cosTheta * 0.5 + 0.5);
  vec3 betaRTheta = vBetaR * rPhase;
  float mPhase = hgPhase(cosTheta, uMieG);
  vec3 betaMTheta = vBetaM * mPhase;
  vec3 Lin = pow(vSunE * ((betaRTheta + betaMTheta) / (vBetaR + vBetaM)) * (1.0 - Fex), vec3(1.5));
  Lin *= mix(vec3(1.0), pow(vSunE * ((betaRTheta + betaMTheta) / (vBetaR + vBetaM)) * Fex, vec3(0.5)), clamp(pow(1.0 - dot(up, sunDir), 5.0), 0.0, 1.0));
  vec3 L0 = vec3(0.1) * Fex;
  float sundisk = smoothstep(0.99995, 0.99998, cosTheta);
  L0 += (vSunE * 19000.0 * Fex) * sundisk;
  vec3 texColor = (Lin + L0) * 0.04 + vec3(0.0, 0.0003, 0.00075);
  return pow(texColor, vec3(1.0 / (1.2 + (1.2 * vSunfade))));
}

void main() {
  vec3 d = normalize(vDir);
  vec3 dd = d;
  dd.y = max(dd.y, 0.0005);
  dd = normalize(dd);
  vec3 Fex;
  vec3 col = scatter(dd, uSunDir, Fex) * uExposure;

  // Night base + city light pollution
  float horizon = exp(-max(d.y, 0.0) * 5.0);
  vec3 nightCol = mix(vec3(0.0006, 0.0011, 0.0035), vec3(0.004, 0.006, 0.014), horizon);
  nightCol += vec3(0.030, 0.016, 0.008) * exp(-max(d.y, 0.0) * 14.0);
  col += nightCol * uNight;

  // Stars
  if (d.y > 0.0 && uNight > 0.01) {
    vec3 p = d * 160.0;
    vec3 cell = floor(p);
    float h = hash13(cell);
    if (h > 0.985) {
      vec3 c = cell + 0.5 + (vec3(hash13(cell + 1.3), hash13(cell + 7.1), hash13(cell + 3.7)) - 0.5) * 0.5;
      float dist = length(p - c);
      float tw = 0.65 + 0.35 * sin(uTime * (2.0 + h * 40.0) + h * 100.0);
      float star = smoothstep(0.22, 0.0, dist) * pow((h - 0.985) / 0.015, 3.0) * tw;
      vec3 sc = mix(vec3(0.7, 0.8, 1.0), vec3(1.0, 0.85, 0.7), hash13(cell + 5.0));
      col += sc * star * 1.6 * uNight * smoothstep(0.0, 0.25, d.y) * (1.0 - uCloudCover * 0.8);
    }
    // milky way band
    float band = exp(-pow(dot(d, normalize(vec3(0.3, 0.2, 1.0))) * 3.0, 2.0));
    col += vec3(0.010, 0.010, 0.016) * band * fbm5(d.xz * 6.0 / (d.y + 0.3)) * uNight;
  }

  // Moon
  float md = dot(d, uMoonDir);
  if (uMoonDir.y > -0.1) {
    float moonDisc = smoothstep(0.99975, 0.99985, md);
    vec2 mp = (d.xz - uMoonDir.xz) * 1800.0;
    float crater = 0.75 + 0.25 * vnoise(mp * 0.5) - 0.15 * smoothstep(0.55, 0.8, vnoise(mp * 1.3));
    col += vec3(1.2, 1.18, 1.1) * moonDisc * crater * (0.4 + uNight * 2.5);
    col += vec3(0.05, 0.06, 0.09) * pow(max(md, 0.0), 800.0) * uNight;
    col += vec3(0.01, 0.012, 0.02) * pow(max(md, 0.0), 30.0) * uNight;
  }

  // Clouds
  if (d.y > 0.0) {
    vec2 uv = d.xz / (d.y + 0.06) * 0.9 + vec2(uTime * 0.0035, uTime * 0.0012);
    float n = fbm5(uv * 1.3);
    float cov = uCloudCover;
    float c = smoothstep(1.0 - cov - 0.05, 1.0 - cov + 0.30, n);
    // wispy high cirrus
    float ci = smoothstep(0.55, 0.9, fbm5(vec2(uv.x * 0.4, uv.y * 2.2) * 1.1 + 40.0)) * 0.35;
    float n2 = fbm5((uv + uSunDir.xz * 0.06) * 1.3);
    float shade = clamp(1.0 - (n2 - n) * 4.0, 0.25, 1.3);
    float fwd = pow(max(dot(d, uSunDir), 0.0), 6.0);
    vec3 lightCol = uSunLight;
    vec3 cloudCol = uAmbient * 0.9 + lightCol * shade * (0.55 + 1.6 * fwd);
    cloudCol *= (1.0 - uCloudDark * 0.7);
    cloudCol += vec3(0.02, 0.022, 0.03) * uNight * shade;
    float fade = smoothstep(0.0, 0.12, d.y);
    col = mix(col, cloudCol, clamp(c * 0.92 + ci * (1.0 - c), 0.0, 1.0) * fade);
  }

  // below horizon: darken towards ground haze color
  if (d.y < 0.0) col *= mix(1.0, 0.55, clamp(-d.y * 4.0, 0.0, 1.0));
  gl_FragColor = vec4(col, 0.0);
}
`;

export class Sky {
  constructor() {
    this.uniforms = {
      uSunDir: { value: new THREE.Vector3(0, 1, 0) },
      uMoonDir: { value: new THREE.Vector3(0, -1, 0) },
      uSunLight: { value: new THREE.Color(1, 1, 1) },
      uAmbient: { value: new THREE.Color(0.4, 0.5, 0.7) },
      uTime: { value: 0 },
      uCloudCover: { value: 0.35 },
      uCloudDark: { value: 0 },
      uNight: { value: 0 },
      uExposure: { value: 0.55 },
      uTurbidity: { value: 6 },
      uRayleigh: { value: 1.6 },
      uMieCoef: { value: 0.006 },
      uMieG: { value: 0.82 },
    };
    const geo = new THREE.SphereGeometry(1, 48, 24);
    this.material = new THREE.ShaderMaterial({
      uniforms: this.uniforms, vertexShader, fragmentShader,
      side: THREE.BackSide, depthWrite: false, depthTest: true,
    });
    this.mesh = new THREE.Mesh(geo, this.material);
    this.mesh.scale.setScalar(2500);
    this.mesh.frustumCulled = false;
    this.mesh.renderOrder = -1000;
  }

  // CPU port of the scattering function for lighting & fog colors (returns linear color *before* exposure).
  sample(dir, sunDir, out = new THREE.Color()) {
    const u = this.uniforms;
    const d = tmpA.copy(dir); d.y = Math.max(d.y, 0.0005); d.normalize();
    const sunIntensity = (zc) => { zc = Math.max(-1, Math.min(1, zc)); return 1000 * Math.max(0, 1 - Math.pow(Math.E, -((1.6110731556870734 - Math.acos(zc)) / 1.5))); };
    const vSunE = sunIntensity(sunDir.y);
    const vSunfade = 1 - Math.max(0, Math.min(1, 1 - Math.exp(sunDir.y)));
    const rc = u.uRayleigh.value - (1 - vSunfade);
    const TR = [5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5];
    const MC = [1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14];
    const cM = 0.2 * u.uTurbidity.value * 10e-18;
    const zen = Math.acos(Math.max(0, d.y));
    const inv = 1 / (Math.cos(zen) + 0.15 * Math.pow(93.885 - zen * 180 / Math.PI, -1.253));
    const sR = 8.4e3 * inv, sM = 1.25e3 * inv;
    const cosT = d.dot(sunDir);
    const rPhase = 0.05968310365946075 * (1 + Math.pow(cosT * 0.5 + 0.5, 2));
    const g = u.uMieG.value, g2 = g * g;
    const mPhase = 0.07957747154594767 * ((1 - g2) / Math.pow(1 - 2 * g * cosT + g2, 1.5));
    const res = [0, 0, 0];
    const mixK = Math.max(0, Math.min(1, Math.pow(1 - sunDir.y, 5)));
    for (let i = 0; i < 3; i++) {
      const bR = TR[i] * rc, bM = 0.434 * cM * MC[i] * u.uMieCoef.value;
      const Fex = Math.exp(-(bR * sR + bM * sM));
      const ratio = (bR * rPhase + bM * mPhase) / (bR + bM);
      let Lin = Math.pow(Math.max(0, vSunE * ratio * (1 - Fex)), 1.5);
      Lin *= 1 + (Math.pow(Math.max(0, vSunE * ratio * Fex), 0.5) - 1) * mixK;
      const L0 = 0.1 * Fex;
      const tex = (Lin + L0) * 0.04 + [0, 0.0003, 0.00075][i];
      res[i] = Math.pow(tex, 1 / (1.2 + 1.2 * vSunfade)) * u.uExposure.value;
    }
    return out.setRGB(res[0], res[1], res[2]);
  }

  // Transmittance of sunlight along the sun direction (color of direct light hitting the ground).
  sunTransmittance(sunDir, out = new THREE.Color()) {
    const u = this.uniforms;
    const y = Math.max(sunDir.y, 0.0);
    const zen = Math.acos(y);
    const inv = 1 / (Math.cos(zen) + 0.15 * Math.pow(Math.max(93.885 - zen * 180 / Math.PI, 0.01), -1.253));
    const TR = [5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5];
    const MC = [1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14];
    const cM = 0.2 * u.uTurbidity.value * 10e-18;
    const res = [];
    for (let i = 0; i < 3; i++) {
      const bR = TR[i] * u.uRayleigh.value, bM = 0.434 * cM * MC[i] * u.uMieCoef.value;
      res.push(Math.exp(-(bR * 8.4e3 * inv + bM * 1.25e3 * inv) * 0.55));
    }
    return out.setRGB(res[0], res[1], res[2]);
  }
}
const tmpA = new THREE.Vector3();
