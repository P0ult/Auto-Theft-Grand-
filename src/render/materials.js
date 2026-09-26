// Shared uniforms + material patching (atmospheric height fog with sun scattering on every lit material,
// rain puddles and ripples, and a reflectivity value written to alpha for the screen-space reflection pass).
import * as THREE from 'three';

export const U = {
  uTime: { value: 0 },
  uSunDir: { value: new THREE.Vector3(0, 1, 0) },
  uFogColor: { value: new THREE.Color(0.6, 0.7, 0.8) },
  uFogSunColor: { value: new THREE.Color(1, 0.8, 0.6) },
  uFogDensity: { value: 0.0011 },
  uFogHeightFalloff: { value: 0.009 },
  uNight: { value: 0 },          // 0 day .. 1 full night
  uWet: { value: 0 },            // rain wetness (how soaked the ground is: lags the rain)
  uRain: { value: 0 },           // rain falling right now (ripples in puddles)
  uStreetLights: { value: 0 },   // street light pools intensity
  uFogFar: { value: new THREE.Vector2(3200, 5600) }, // distance fog fully swallows the world
};

export const FOG_GLSL = /* glsl */`
uniform vec3 uFogColor;
uniform vec3 uFogSunColor;
uniform vec3 uSunDir;
uniform float uFogDensity;
uniform float uFogHeightFalloff;
uniform vec2 uFogFar;
vec3 atgFog(vec3 col, vec3 wpos) {
  vec3 v = wpos - cameraPosition;
  float d = length(v);
  vec3 dir = v / max(d, 1e-4);
  float fh = uFogHeightFalloff;
  float dy = v.y;
  float k = abs(dy * fh) > 0.001 ? (1.0 - exp(-fh * dy)) / (fh * dy) : 1.0;
  float amt = uFogDensity * exp(-fh * max(cameraPosition.y, -5.0)) * d * k;
  amt = 1.0 - exp(-amt);
  amt = max(amt, smoothstep(uFogFar.x, uFogFar.y, d));
  float sunAmt = pow(max(dot(dir, uSunDir), 0.0), 6.0);
  vec3 fc = mix(uFogColor, uFogSunColor, sunAmt);
  return mix(col, fc, clamp(amt, 0.0, 1.0));
}
`;

// Rain on surfaces. atgRefl (0..1) is how mirror-like the pixel is: opaque materials store it in alpha
// (1 - 0.5 * refl; the sky keeps alpha 0) and the post-processing traces screen-space reflections there.
export const WET_GLSL = /* glsl */`
uniform float uRain;
float atgRefl = 0.0;
float atgPud = 0.0;
float atgH21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float atgVN(vec2 p) {
  vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(atgH21(i), atgH21(i + vec2(1.0, 0.0)), u.x), mix(atgH21(i + vec2(0.0, 1.0)), atgH21(i + vec2(1.0, 1.0)), u.x), u.y);
}
// Standing water: low spots that fill up as the ground gets wetter (only on level ground)
float atgPuddle(vec2 xz) {
  if (uWet < 0.02) return 0.0;
  vec3 fn = cross(dFdx(vAtgWorld), dFdy(vAtgWorld));
  float flat_ = smoothstep(0.965, 0.995, abs(fn.y) / max(length(fn), 1e-6));
  float n = atgVN(xz * 0.075) * 0.55 + atgVN(xz * 0.23 + 7.3) * 0.3 + atgVN(xz * 0.9 + 3.1) * 0.15;
  float thr = mix(0.76, 0.5, uWet);
  return smoothstep(thr, thr + 0.035, n) * flat_;
}
// Raindrops landing in water: rings spreading from random points (a slope in xz)
vec2 atgRipples(vec2 xz) {
  if (uRain < 0.02) return vec2(0.0);
  vec2 g = vec2(0.0);
  for (int k = 0; k < 3; k++) {
    vec2 p = xz * 2.3 + vec2(float(k) * 0.43, float(k) * 0.77);
    vec2 cell = floor(p);
    float h = atgH21(cell + float(k) * 17.0);
    if (h > uRain * 0.85 + 0.15) continue; // fewer drops in light rain
    vec2 c = cell + 0.3 + 0.4 * vec2(h, atgH21(cell.yx + 3.7 + float(k)));
    float ph = fract(uTime * 0.9 + h * 13.0);
    vec2 dv = p - c;
    float d = length(dv);
    float x = (d - ph * 0.3) * 40.0;
    float ring = sin(x) * (1.0 - smoothstep(0.0, 3.1416, abs(x)));
    g += dv / max(d, 1e-3) * ring * (1.0 - ph) * (1.0 - ph);
  }
  return g * 0.4 * clamp(uRain * 1.5, 0.0, 1.0);
}
// Rain on ground-like surfaces: darker, glossier, puddles of clear water (porous: 0 asphalt .. 1 soil)
void atgWetGround(inout vec3 col, inout float rough, float porous) {
  if (uWet < 0.005) return;
  float pud = atgPuddle(vAtgWorld.xz) * (1.0 - porous * 0.6);
  col *= 1.0 - uWet * (0.22 + 0.2 * porous);
  col = mix(col, col * 0.42 + vec3(0.006, 0.008, 0.01), pud);
  rough = mix(rough, mix(0.3, 0.6, porous), uWet * 0.75);
  rough = mix(rough, 0.03, pud);
  atgRefl = max(atgRefl, uWet * (0.14 - 0.1 * porous) + pud * 0.85);
  atgPud = max(atgPud, pud);
}
`;

// ripples in puddles (view-space normal), used by ground materials after atgWetGround
export const WET_NORMAL = /* glsl */`
if (atgPud > 0.01) {
  vec2 rp = atgRipples(vAtgWorld.xz);
  vec3 wn = normalize(vec3(-rp.x, 1.0, -rp.y));
  normal = normalize(mix(normal, normalize((viewMatrix * vec4(wn, 0.0)).xyz), atgPud));
}
`;

const VERT_WORLD = /* glsl */`
{
  vec4 atgW = vec4( transformed, 1.0 );
  #ifdef USE_BATCHING
    atgW = batchingMatrix * atgW;
  #endif
  #ifdef USE_INSTANCING
    atgW = instanceMatrix * atgW;
  #endif
  vAtgWorld = ( modelMatrix * atgW ).xyz;
}
`;

/**
 * Patch a built-in material. `ext` may contain:
 *  uniforms: {name: uniformObj}
 *  vertexPars, vertexMain (after world pos is computed; `vAtgWorld` available),
 *  fragPars, fragColor (runs right after <color_fragment>; modify diffuseColor),
 *  fragRoughness (after <roughnessmap_fragment>), fragMetal, fragEmissive (after <emissivemap_fragment>),
 *  fragNormal (after <normal_fragment_maps>), fragEnd (before fog, modify gl_FragColor)
 *  key: cache key
 */
export function patch(material, ext = {}) {
  const key = 'atg:' + (ext.key || 'base');
  material.customProgramCacheKey = () => key;
  material.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, {
      uFogColor: U.uFogColor, uFogSunColor: U.uFogSunColor, uSunDir: U.uSunDir,
      uFogDensity: U.uFogDensity, uFogHeightFalloff: U.uFogHeightFalloff,
      uTime: U.uTime, uNight: U.uNight, uWet: U.uWet, uRain: U.uRain, uStreetLights: U.uStreetLights, uFogFar: U.uFogFar,
    }, ext.uniforms || {});
    let vs = shader.vertexShader;
    vs = 'varying vec3 vAtgWorld;\nuniform float uTime;\n' + (ext.vertexPars || '') + '\n' + vs;
    vs = vs.replace('#include <fog_vertex>', '#include <fog_vertex>\n' + VERT_WORLD + (ext.vertexMain || ''));
    if (ext.vertexBegin) vs = vs.replace('#include <begin_vertex>', '#include <begin_vertex>\n' + ext.vertexBegin);
    shader.vertexShader = vs;

    let fs = shader.fragmentShader;
    fs = 'varying vec3 vAtgWorld;\nuniform float uTime;\nuniform float uNight;\nuniform float uWet;\nuniform float uStreetLights;\n' + FOG_GLSL + WET_GLSL + (ext.fragPars || '') + '\n' + fs;
    if (ext.fragColor) fs = fs.replace('#include <color_fragment>', '#include <color_fragment>\n' + ext.fragColor);
    if (ext.fragRoughness) fs = fs.replace('#include <roughnessmap_fragment>', '#include <roughnessmap_fragment>\n' + ext.fragRoughness);
    if (ext.fragMetal) fs = fs.replace('#include <metalnessmap_fragment>', '#include <metalnessmap_fragment>\n' + ext.fragMetal);
    if (ext.fragNormal) fs = fs.replace('#include <normal_fragment_maps>', '#include <normal_fragment_maps>\n' + ext.fragNormal);
    if (ext.fragEmissive) fs = fs.replace('#include <emissivemap_fragment>', '#include <emissivemap_fragment>\n' + ext.fragEmissive);
    fs = fs.replace('#include <fog_fragment>', (ext.fragEnd || '') + '\ngl_FragColor.rgb = atgFog(gl_FragColor.rgb, vAtgWorld);\n#ifdef OPAQUE\ngl_FragColor.a = 1.0 - 0.5 * clamp(atgRefl, 0.0, 1.0);\n#endif');
    shader.fragmentShader = fs;
  };
  return material;
}

export function std(params = {}, ext) {
  return patch(new THREE.MeshStandardMaterial(params), ext);
}

// Simple unlit material that still receives fog (for glowing things).
export function basicFog(params = {}, key = 'basic') {
  return patch(new THREE.MeshBasicMaterial(params), { key });
}

// Emissive-only material for lamps, windows, lights, respects night factor through intensity changes by caller.
export function glow(color, intensity = 1) {
  const m = new THREE.MeshBasicMaterial({ color: new THREE.Color(color).multiplyScalar(intensity) });
  return patch(m, { key: 'glow' });
}
