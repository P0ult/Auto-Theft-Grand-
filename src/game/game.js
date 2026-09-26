// Game: owns the renderer, world, entities and all gameplay systems; runs the main loop.
import * as THREE from 'three';
import { PostFX } from '../render/postfx.js';
import { Environment } from '../world/environment.js';
import { CityMap, WATER_Y } from '../world/citymap.js';
import { LAKE } from '../world/worldgen.js';
import { U } from '../render/materials.js';
import { CollisionWorld } from '../world/collision.js';
import { City } from '../world/city.js';
import { Input } from '../core/input.js';
import { Events } from '../core/events.js';
import { CameraRig } from './camera.js';
import { Player } from './player.js';
import { VehicleManager } from './vehicles.js';
import { clamp } from '../core/utils.js';

export const QUALITY = {
  low: { pixelRatio: 0.75, shadows: 1024, shadowSize: 60, msaa: 0, bloom: false, rays: false, ssr: 0, ao: false, peds: 18, traffic: 14, drawDist: 1400 },
  medium: { pixelRatio: 1, shadows: 2048, shadowSize: 80, msaa: 2, bloom: true, rays: false, ssr: 16, ao: false, peds: 28, traffic: 22, drawDist: 2200 },
  high: { pixelRatio: 1, shadows: 2048, shadowSize: 95, msaa: 4, bloom: true, rays: true, ssr: 24, ao: true, peds: 38, traffic: 28, drawDist: 3000 },
  ultra: { pixelRatio: Math.min(2, window.devicePixelRatio || 1), shadows: 4096, shadowSize: 110, msaa: 4, bloom: true, rays: true, ssr: 36, ao: true, peds: 46, traffic: 34, drawDist: 3200 },
};

export class Game {
  constructor(container, opts = {}) {
    this.container = container;
    this.opts = opts;
    this.events = new Events();
    this.time = 0;
    this.paused = false;
    this.timeScale = 1;
    this.settings = Object.assign({ quality: 'high', volume: 0.8, music: 0.6, sensitivity: 1, invertY: false }, opts.settings || {});
    this.systems = [];
    this.blips = new Set();
    this.stats = { kills: 0, copKills: 0, headshots: 0, carsStolen: 0, carsDestroyed: 0, runOver: 0, wasted: 0, busted: 0, maxWanted: 0, bestDrift: 0, driven: 0, walked: 0, playTime: 0, missions: 0, sprays: 0 };
  }

  async init(progress = () => {}) {
    const q = QUALITY[this.settings.quality] || QUALITY.high;
    this.quality = q;
    // reversed float depth: stable depth precision from 25 cm to many kilometres (no distant z-fighting)
    const renderer = new THREE.WebGLRenderer({ antialias: false, powerPreference: 'high-performance', stencil: false, reversedDepthBuffer: !this.opts.noReversedDepth });
    renderer.setPixelRatio(q.pixelRatio);
    renderer.setSize(window.innerWidth, window.innerHeight);
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFShadowMap;
    renderer.setClearColor(0x000000, 0);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.container.appendChild(renderer.domElement);
    this.renderer = renderer;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(62, window.innerWidth / window.innerHeight, 0.25, 9000);
    this.scene.add(this.camera);
    progress(0.05, 'Surveying Los Soles');
    await tick();
    this.map = new CityMap(1337);
    this.collision = new CollisionWorld(this.map);
    progress(0.2, 'Raising the skyline');
    await tick();
    this.city = new City(this.scene, this.map, this.collision);
    progress(0.55, 'Painting the sky');
    await tick();
    this.env = new Environment(renderer, this.scene);
    this.env.shadowSize = q.shadowSize;
    this.env.setShadowQuality(q.shadows);
    this.post = new PostFX(renderer);
    this.post.samples = q.msaa;
    this.post.bloomEnabled = q.bloom;
    this.post.raysEnabled = q.rays;
    this._applyFx(q);
    this.post.setSize(window.innerWidth * q.pixelRatio, window.innerHeight * q.pixelRatio);
    const wu = this.city.waterUniforms;
    if (wu) this.post.setWater(wu.uDepthTex.value, wu.uWorldRect.value, WATER_Y, LAKE);

    this.input = new Input(renderer.domElement);
    this.input.sensitivity = this.settings.sensitivity;
    this.input.invertY = this.settings.invertY;
    this.rig = new CameraRig(this.camera, this);
    progress(0.65, 'Waking up the neighborhood');
    await tick();
    this.player = new Player(this);
    this.vehicles = new VehicleManager(this);

    // player headlights (one spotlight, reused)
    this.headlight = new THREE.SpotLight(0xfff2dd, 0, 60, 0.55, 0.5, 1.2);
    this.headlight.castShadow = false;
    this.scene.add(this.headlight);
    this.scene.add(this.headlight.target);

    // optional systems are registered by main.js (peds, traffic, police, combat, effects, audio, hud, missions)
    window.addEventListener('resize', () => this.resize());
    progress(0.75, 'Loading systems');
    return this;
  }

  addSystem(name, sys) { this[name] = sys; this.systems.push({ name, sys }); return sys; }

  allCharacters() {
    const list = [this.player];
    if (this.peds) for (const p of this.peds.list) list.push(p);
    if (this.net) for (const a of this.net.avatarList) list.push(a);
    return list;
  }

  resize() {
    const q = this.quality;
    this.renderer.setSize(window.innerWidth, window.innerHeight);
    this.camera.aspect = window.innerWidth / window.innerHeight;
    this.camera.updateProjectionMatrix();
    this.post.setSize(window.innerWidth * q.pixelRatio, window.innerHeight * q.pixelRatio);
  }

  applyQuality(name) {
    const q = QUALITY[name] || QUALITY.high;
    this.settings.quality = name;
    this.quality = q;
    this.renderer.setPixelRatio(q.pixelRatio);
    this.env.shadowSize = q.shadowSize;
    this.env.setShadowQuality(q.shadows);
    this.post.samples = q.msaa;
    this.post.bloomEnabled = q.bloom;
    this.post.raysEnabled = q.rays;
    this._applyFx(q);
    this.resize();
    if (this.peds) this.peds.maxPeds = q.peds;
    if (this.traffic) this.traffic.maxCars = q.traffic;
  }

  // screen-space reflections & ambient occlusion: the quality preset, unless switched on / off in Settings
  _applyFx(q = this.quality) {
    const s = this.settings;
    const ssr = s.reflections == null ? q.ssr > 0 : !!s.reflections;
    this.post.ssrEnabled = ssr;
    this.post.ssr.uniforms.uSteps.value = Math.max(q.ssr, 16);
    this.post.aoEnabled = s.ao == null ? q.ao : !!s.ao;
  }

  start() {
    this.last = performance.now();
    this.running = true;
    const loop = () => {
      if (!this.running) return;
      requestAnimationFrame(loop);
      const now = performance.now();
      let dt = (now - this.last) / 1000;
      this.last = now;
      dt = Math.min(dt, 1 / 20);
      this.frame(dt);
    };
    requestAnimationFrame(loop);
  }

  frame(dt) {
    const input = this.input;
    input.pollGamepad();
    const sdt = this.paused ? 0 : dt * this.timeScale;
    if (!this.paused) this.update(sdt, dt);
    this.net?.tick(dt);
    this.hud?.update(dt);
    this.render(dt);
    input.endFrame();
  }

  update(dt, realDt) {
    this.time += dt;
    const player = this.player;
    const input = this.input;
    const controlsEnabled = input.enabled && !this.cutscene;

    // player control
    if (controlsEnabled) {
      if (!player.vehicle) player.control(dt, input, this.rig);
      else if (player.seat === 0 && !this.vehicles.isBusy(player)) player.vehicle.playerControl(input, dt);
      if (input.hit('enter') && !player.dead && !player.ragdolling) this.tryEnterExit();
      if (player.vehicle) {
        this.rig.lookBehind = input.down('lookBehind');
        if (input.hit('camera')) this.rig.vehicleCamIndex++;
        const pv0 = player.vehicle;
        if (pv0.armed && player.seat === 0) player.aiming = !!pv0.showCrosshair; // mounted guns fire from playerControl
        else {
          player.aiming = input.aimDown() && player.weaponDef.type === 'gun' && (player.weapon === 'pistol' || player.weapon === 'smg') && !pv0.def.kind;
          if (player.aiming) {
            player.fireCooldown -= dt;
            if (input.mouse.left && player.fireCooldown <= 0) player.fire(this.rig);
          }
        }
      }
    } else {
      player.moveTarget.set(0, 0);
      if (player.vehicle && player.seat === 0 && this.cutscene) { player.vehicle.input.throttle = 0; player.vehicle.input.brake = 0.5; player.vehicle.input.steer = 0; }
    }

    for (const { sys } of this.systems) if (sys.preUpdate) sys.preUpdate(dt);
    this.vehicles.update(dt);
    player.update(dt);
    for (const { sys } of this.systems) if (sys.update && sys !== this.hud) sys.update(dt);

    // headlight follows the player's car at night
    const pv = player.vehicle;
    if (pv && pv.lightsOn) {
      const f = pv.fwd;
      this.headlight.position.copy(pv.pos).addScaledVector(f, pv.def.L / 2).setY(pv.pos.y + 0.8);
      this.headlight.target.position.copy(this.headlight.position).addScaledVector(f, 20).setY(pv.pos.y - 1.5);
      this.headlight.intensity = 60;
    } else this.headlight.intensity = 0;

    // environment & camera
    const focus = pv ? pv.pos : player.ragdolling ? player.ragdoll.center : player.pos;
    this.env.update(dt, focus);
    this.rig.update(realDt ?? dt, controlsEnabled ? input : null, player);
    this.env.sky.mesh.position.copy(this.camera.position);
    this.city.update(dt, this.camera.position, this.env.night);
  }

  tryEnterExit() {
    const p = this.player;
    if (this.vehicles.isBusy(p)) return;
    if (p.vehicle) { if (this.taxi?.handleExit(p)) return; this.vehicles.exit(p); return; }
    const v = this.vehicles.nearestEnterable(p.pos, 5);
    if (v) {
      if (this.missions?.canEnterVehicle && !this.missions.canEnterVehicle(v)) return;
      const cabSeat = this.taxi?.seatFor(v, p); // a cab you whistled for: get in the back
      if (cabSeat != null) { this.vehicles.enter(p, v, cabSeat); return; }
      const seat = v.nearestDoor ? v.nearestDoor(p.pos).seat : 0;
      if (seat > 0 && v.occupants[seat]) { const free = [1, 2, 3].find((k) => !v.occupants[k]); if (free) return this.vehicles.enter(p, v, free); }
      this.vehicles.enter(p, v, seat);
    }
  }

  render(dt) {
    const cam = this.camera;
    const env = this.env;
    // sun screen position for god rays
    const sp = new THREE.Vector3().copy(cam.position).addScaledVector(env.sunDir, 1000).project(cam);
    let vis = 0;
    if (sp.z < 1 && env.sunDir.y > -0.05) {
      const edge = Math.max(Math.abs(sp.x), Math.abs(sp.y));
      vis = env.sunVisible * clamp(1.4 - edge, 0, 1) * (0.35 + 0.65 * clamp(1 - env.sunDir.y * 1.5, 0, 1));
    }
    const c = this.post.composite.uniforms;
    c.uExposure.value = 1.0 + env.night * 1.0;
    this.post.time = this.time;
    this.post.ssr.uniforms.uRain.value = U.uRain.value;
    this.post.ssr.uniforms.uWet.value = U.uWet.value;
    c.uTime.value = this.time;
    this.post.render(this.scene, cam, new THREE.Vector2(sp.x * 0.5 + 0.5, sp.y * 0.5 + 0.5), vis * 0.45);
  }
}

function tick() { return new Promise((r) => setTimeout(r, 0)); }
