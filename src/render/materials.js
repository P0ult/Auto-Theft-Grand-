// Shared uniforms + material patching (atmospheric height fog with sun scattering on every lit material).
import * as THREE from 'three';

export const U = {
  uTime: { value: 0 },
  uSunDir: { value: new THREE.Vector3(0, 1, 0) },
  uFogColor: { value: new THREE.Color(0.6, 0.7, 0.8) },
  uFogSunColor: { value: new THREE.Color(1, 0.8, 0.6) },
  uFogDensity: { value: 0.0011 },
  uFogHeightFalloff: { value: 0.009 },
  uNight: { value: 0 },          // 0 day .. 1 full night
  uWet: { value: 0 },            // rain wetness
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
      uTime: U.uTime, uNight: U.uNight, uWet: U.uWet, uStreetLights: U.uStreetLights, uFogFar: U.uFogFar,
    }, ext.uniforms || {});
    let vs = shader.vertexShader;
    vs = 'varying vec3 vAtgWorld;\nuniform float uTime;\n' + (ext.vertexPars || '') + '\n' + vs;
    vs = vs.replace('#include <fog_vertex>', '#include <fog_vertex>\n' + VERT_WORLD + (ext.vertexMain || ''));
    if (ext.vertexBegin) vs = vs.replace('#include <begin_vertex>', '#include <begin_vertex>\n' + ext.vertexBegin);
    shader.vertexShader = vs;

    let fs = shader.fragmentShader;
    fs = 'varying vec3 vAtgWorld;\nuniform float uTime;\nuniform float uNight;\nuniform float uWet;\nuniform float uStreetLights;\n' + FOG_GLSL + (ext.fragPars || '') + '\n' + fs;
    if (ext.fragColor) fs = fs.replace('#include <color_fragment>', '#include <color_fragment>\n' + ext.fragColor);
    if (ext.fragRoughness) fs = fs.replace('#include <roughnessmap_fragment>', '#include <roughnessmap_fragment>\n' + ext.fragRoughness);
    if (ext.fragMetal) fs = fs.replace('#include <metalnessmap_fragment>', '#include <metalnessmap_fragment>\n' + ext.fragMetal);
    if (ext.fragNormal) fs = fs.replace('#include <normal_fragment_maps>', '#include <normal_fragment_maps>\n' + ext.fragNormal);
    if (ext.fragEmissive) fs = fs.replace('#include <emissivemap_fragment>', '#include <emissivemap_fragment>\n' + ext.fragEmissive);
    fs = fs.replace('#include <fog_fragment>', (ext.fragEnd || '') + '\ngl_FragColor.rgb = atgFog(gl_FragColor.rgb, vAtgWorld);');
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
