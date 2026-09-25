// HDR post-processing: MSAA scene target -> bloom mip chain -> god rays -> filmic composite.
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
    float sky = 1.0 - s.a;
    vec3 c = min(s.rgb, vec3(4.0)) * sky;
    acc += c * illum;
    illum *= 0.965;
  }
  gl_FragColor = vec4(acc / 40.0 * uStrength, 1.0);
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
  // vignette
  float vig = smoothstep(0.85, 0.15, r2 * uVignette * 2.2);
  col *= mix(1.0, vig, 0.55);
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
    });
    this.blackTex = new THREE.DataTexture(new Uint8Array([0, 0, 0, 255]), 1, 1);
    this.blackTex.needsUpdate = true;
    this.targets = null;
    this.width = 0; this.height = 0;
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
    }
    const sw = Math.max(1, Math.floor(w * this.scale)), sh = Math.max(1, Math.floor(h * this.scale));
    const scene = this._makeRT(sw, sh, this.samples);
    const mips = [], ups = [];
    let mw = Math.max(1, sw >> 1), mh = Math.max(1, sh >> 1);
    for (let i = 0; i < 6; i++) {
      const rt = this._makeRT(mw, mh); rt.depthBuffer = false; mips.push(rt);
      const u = this._makeRT(mw, mh); ups.push(u);
      mw = Math.max(1, mw >> 1); mh = Math.max(1, mh >> 1);
    }
    const rays = this._makeRT(Math.max(1, sw >> 2), Math.max(1, sh >> 2));
    this.targets = { scene, mips, ups, rays, sw, sh };
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

    const c = this.composite.uniforms;
    c.tScene.value = T.scene.texture;
    c.tBloom.value = bloomTex;
    c.tRays.value = raysOn ? T.rays.texture : this.blackTex;
    c.uRaysOn.value = raysOn ? 1 : 0;
    this._pass(this.composite, null);
  }
}
