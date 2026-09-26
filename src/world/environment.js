// Time of day, sun/moon lighting, shadows following the player, environment reflections, weather.
import * as THREE from 'three';
import { Sky } from '../render/sky.js';
import { U } from '../render/materials.js';
import { clamp, smoothstep, lerp } from '../core/utils.js';

const _v = new THREE.Vector3();
const _c = new THREE.Color();
const _c2 = new THREE.Color();

export class Environment {
  constructor(renderer, scene) {
    this.renderer = renderer;
    this.scene = scene;
    this.hours = 8.5;          // game clock
    this.timeScale = 1;        // game minutes per real second
    this.sky = new Sky();
    scene.add(this.sky.mesh);

    this.sunDir = new THREE.Vector3();
    this.moonDir = new THREE.Vector3();
    this.lightDir = new THREE.Vector3();
    this.sunVisible = 1;

    this.sun = new THREE.DirectionalLight(0xffffff, 3);
    this.sun.castShadow = true;
    this.shadowSize = 90;
    this.setShadowQuality(2048);
    scene.add(this.sun);
    scene.add(this.sun.target);

    this.hemi = new THREE.HemisphereLight(0x8899bb, 0x443322, 0.6);
    scene.add(this.hemi);

    this.pmrem = new THREE.PMREMGenerator(renderer);
    this.envScene = new THREE.Scene();
    this.envSky = new THREE.Mesh(this.sky.mesh.geometry, this.sky.material);
    this.envSky.scale.setScalar(50);
    this.envScene.add(this.envSky);
    // a dark ground disc so reflections have a horizon
    const ground = new THREE.Mesh(new THREE.CircleGeometry(40, 24), new THREE.MeshBasicMaterial({ color: 0x0a0a0a }));
    ground.rotation.x = -Math.PI / 2; ground.position.y = -2;
    this.envGround = ground;
    this.envScene.add(ground);
    this.envRT = null;
    this.envTimer = 0;
    this.envInterval = 1.5;

    // weather
    this.weather = 'clear';
    this.cloudCover = 0.35;
    this.rain = 0;
    this.targetRain = 0;
    this.targetCloud = 0.35;
    this.fogBoost = 0;
    this.weatherTimer = 240;
    this.lightning = 0;

    this.night = 0;
    this.sunLightColor = new THREE.Color();
    this.ambientColor = new THREE.Color();
    this.update(0, new THREE.Vector3());
    this.updateEnvMap();
  }

  setShadowQuality(size) {
    const s = this.sun.shadow;
    s.mapSize.set(size, size);
    if (s.map) { s.map.dispose(); s.map = null; }
    const half = this.shadowSize;
    s.camera.left = -half; s.camera.right = half; s.camera.top = half; s.camera.bottom = -half;
    s.camera.near = 1; s.camera.far = 600;
    s.bias = -0.0004;
    s.normalBias = 0.04;
    s.radius = 2;
    s.camera.updateProjectionMatrix();
  }

  setTime(h) {
    this.hours = ((h % 24) + 24) % 24;
    this.update(0, null);
    this.updateEnvMap();
  }

  get timeString() {
    const h = Math.floor(this.hours), m = Math.floor((this.hours - h) * 60);
    return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
  }

  setWeather(w, instant = false) {
    this.weather = w;
    const cfg = { clear: [0.25, 0], cloudy: [0.7, 0], rain: [0.9, 1], storm: [1, 1], fog: [0.5, 0] }[w] || [0.3, 0];
    this.targetCloud = cfg[0]; this.targetRain = cfg[1];
    this.fogTarget = w === 'fog' ? 1 : w === 'rain' || w === 'storm' ? 0.4 : 0;
    if (instant) { this.cloudCover = this.targetCloud; this.rain = this.targetRain; this.fogBoost = this.fogTarget; }
  }

  update(dt, focus) {
    this.hours = (this.hours + dt * this.timeScale / 60) % 24;
    const theta = (this.hours - 6) / 24 * Math.PI * 2;
    const tilt = 0.45;
    this.sunDir.set(Math.cos(theta), Math.sin(theta) * Math.cos(tilt) + 0.2, Math.sin(theta) * Math.sin(tilt) + 0.08).normalize();
    this.moonDir.set(-Math.cos(theta) * 0.9, -Math.sin(theta) * Math.cos(tilt) * 0.95 + 0.12, -Math.sin(theta) * Math.sin(tilt) - 0.2).normalize();

    // weather transitions
    this.weatherTimer -= dt * this.timeScale;
    if (this.weatherTimer <= 0 && !this.weatherLocked) {
      this.weatherTimer = 300 + Math.random() * 600;
      const r = Math.random();
      this.setWeather(r < 0.55 ? 'clear' : r < 0.78 ? 'cloudy' : r < 0.92 ? 'rain' : r < 0.96 ? 'storm' : 'fog');
    }
    this.cloudCover = lerp(this.cloudCover, this.targetCloud, clamp(dt * 0.05, 0, 1));
    this.rain = lerp(this.rain, this.targetRain, clamp(dt * 0.08, 0, 1));
    this.fogBoost = lerp(this.fogBoost, this.fogTarget || 0, clamp(dt * 0.05, 0, 1));
    U.uWet.value = clamp(lerp(U.uWet.value, this.rain > 0.2 ? 1 : 0, dt * (this.rain > 0.2 ? 0.05 : 0.01)), 0, 1);
    U.uRain.value = this.rain;
    if (this.weather === 'storm' && Math.random() < dt * 0.08) this.lightning = 1;
    this.lightning = Math.max(0, this.lightning - dt * 3.5);

    const sunY = this.sunDir.y;
    this.night = 1 - smoothstep(-0.18, 0.06, sunY);
    const dayF = smoothstep(-0.04, 0.12, sunY);

    const su = this.sky.uniforms;
    su.uSunDir.value.copy(this.sunDir);
    su.uMoonDir.value.copy(this.moonDir);
    su.uNight.value = this.night;
    su.uCloudCover.value = this.cloudCover;
    su.uCloudDark.value = this.rain * 0.8;
    su.uTime.value += dt;
    su.uTurbidity.value = 5 + this.cloudCover * 3;

    // Sunlight color: transmittance through atmosphere
    this.sky.sunTransmittance(this.sunDir, _c);
    const sunI = smoothstep(-0.02, 0.1, sunY) * (1 - this.cloudCover * 0.55) * (1 - this.rain * 0.4);
    this.sunLightColor.copy(_c).multiplyScalar(sunI);
    // ambient from sky zenith
    this.sky.sample(_v.set(0, 1, 0), this.sunDir, this.ambientColor);
    this.ambientColor.multiplyScalar(1.4);
    const nightAmb = _c2.setRGB(0.085, 0.105, 0.19);
    this.ambientColor.lerp(nightAmb, this.night);
    if (this.cloudCover > 0.5) { const g = (this.ambientColor.r + this.ambientColor.g + this.ambientColor.b) / 3; this.ambientColor.lerp(_c2.setRGB(g, g, g * 1.05), (this.cloudCover - 0.5) * 1.4); }
    su.uSunLight.value.copy(this.sunLightColor).multiplyScalar(1.1).add(_c2.setRGB(0.02, 0.025, 0.04).multiplyScalar(this.night));
    su.uAmbient.value.copy(this.ambientColor);

    // Directional light: sun during day, moon at night
    const moonMode = sunY < -0.04;
    this.lightDir.copy(moonMode ? this.moonDir : this.sunDir);
    if (this.lightDir.y < 0.12) { this.lightDir.y = 0.12; this.lightDir.normalize(); }
    if (moonMode) {
      this.sun.color.setRGB(0.55, 0.65, 1.0);
      this.sun.intensity = 0.8 * smoothstep(-0.05, 0.2, this.moonDir.y) * (1 - this.cloudCover * 0.5);
    } else {
      this.sun.color.copy(_c).multiplyScalar(1 / Math.max(_c.r, _c.g, _c.b, 0.001));
      this.sun.intensity = 3.4 * sunI * Math.max(_c.r, _c.g, _c.b);
    }
    if (this.lightning > 0) { this.sun.intensity += this.lightning * 6; this.sun.color.setRGB(0.8, 0.85, 1); }
    this.sunVisible = moonMode ? 0 : sunI;

    // Hemisphere ambient
    this.hemi.color.copy(this.ambientColor).multiplyScalar(1.0);
    this.hemi.groundColor.setRGB(0.18, 0.15, 0.12).multiplyScalar(0.25 + dayF * 0.75).multiply(_c2.setRGB(1, 1, 1).lerp(this.sun.color, 0.4));
    this.hemi.intensity = 0.9 + this.night * 1.4 + this.lightning * 3;

    // Fog colors from horizon
    this.sky.sample(_v.set(-this.sunDir.x, 0.04, -this.sunDir.z).normalize(), this.sunDir, U.uFogColor.value);
    this.sky.sample(_v.set(this.sunDir.x, 0.03, this.sunDir.z).normalize(), this.sunDir, U.uFogSunColor.value);
    U.uFogColor.value.lerp(_c2.setRGB(0.012, 0.014, 0.022), this.night * 0.95);
    U.uFogSunColor.value.lerp(_c2.setRGB(0.018, 0.016, 0.022), this.night * 0.95);
    // smoggy haze tint like a big west-coast city
    U.uFogColor.value.lerp(_c2.setRGB(0.75, 0.68, 0.55).multiplyScalar(0.6 * dayF + 0.02), 0.15 * dayF);
    if (this.cloudCover > 0.6 || this.rain > 0.1) {
      const g = U.uFogColor.value.getHSL({}).l;
      U.uFogColor.value.lerp(_c2.setRGB(g, g * 1.02, g * 1.06), clamp(this.rain + (this.cloudCover - 0.6), 0, 0.8));
    }
    U.uFogDensity.value = 0.00105 + this.fogBoost * 0.009 + this.rain * 0.002;
    U.uSunDir.value.copy(this.sunDir);
    U.uNight.value = this.night;
    U.uStreetLights.value = smoothstep(0.35, 0.75, this.night);
    U.uTime.value += dt;

    // Shadow camera follows focus (texel snapped to avoid shimmering)
    if (focus) {
      const s = this.sun;
      const texel = (this.shadowSize * 2) / s.shadow.mapSize.x;
      // snap in light space
      const ld = this.lightDir;
      const up = Math.abs(ld.y) > 0.99 ? _v.set(1, 0, 0) : _v.set(0, 1, 0);
      const right = new THREE.Vector3().crossVectors(up, ld).normalize();
      const upv = new THREE.Vector3().crossVectors(ld, right).normalize();
      let rx = focus.dot(right), uy = focus.dot(upv);
      const fz = focus.dot(ld);
      rx = Math.round(rx / texel) * texel; uy = Math.round(uy / texel) * texel;
      const center = new THREE.Vector3().addScaledVector(right, rx).addScaledVector(upv, uy).addScaledVector(ld, fz);
      s.target.position.copy(center);
      s.position.copy(center).addScaledVector(ld, 300);
      s.target.updateMatrixWorld();
    }

    this.envTimer += dt;
    if (this.envTimer > this.envInterval) { this.envTimer = 0; this.updateEnvMap(); }
  }

  updateEnvMap() {
    this.envGround.material.color.copy(this.ambientColor).multiplyScalar(0.25);
    if (!this.cubeRT) {
      this.cubeRT = new THREE.WebGLCubeRenderTarget(64, { type: THREE.HalfFloatType });
      this.cubeCam = new THREE.CubeCamera(0.1, 200, this.cubeRT);
      this.envScene.add(this.cubeCam);
    }
    this.cubeCam.update(this.renderer, this.envScene);
    this.envRT = this.pmrem.fromCubemap(this.cubeRT.texture, this.envRT || null);
    this.scene.environment = this.envRT.texture;
    this.scene.environmentIntensity = 0.55 + (1 - this.night) * 0.45;
  }
}
