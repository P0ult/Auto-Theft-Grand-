// Vehicle catalogue. Dimensions in meters, forces in newtons, speeds in m/s.
export const VEHICLES = {
  meridian: {
    name: 'Meridian', body: 'sedan', L: 4.8, W: 1.86, H: 1.45, wheelbase: 2.8, track: 1.58, wheelR: 0.34, clearance: 0.26,
    mass: 1450, force: 8200, top: 47, grip: 1.0, drive: 'rwd', steer: 0.62, brake: 15000, rarity: 10,
    colors: [0x8c1c13, 0x1d3557, 0xe8e8e8, 0x2b2b2b, 0x6c757d, 0x3a5a40, 0xbc6c25, 0x5e548e],
  },
  kestrel: {
    name: 'Kestrel GT', body: 'coupe', L: 4.5, W: 1.88, H: 1.3, wheelbase: 2.62, track: 1.6, wheelR: 0.34, clearance: 0.2,
    mass: 1350, force: 12500, top: 60, grip: 1.12, drive: 'rwd', steer: 0.6, brake: 18000, rarity: 4,
    colors: [0xd00000, 0xffba08, 0x0077b6, 0x111111, 0xf1faee, 0x2dc653],
  },
  brawler: {
    name: 'Brawler', body: 'muscle', L: 5.0, W: 1.95, H: 1.35, wheelbase: 2.95, track: 1.62, wheelR: 0.36, clearance: 0.24,
    mass: 1600, force: 13000, top: 55, grip: 0.9, drive: 'rwd', steer: 0.6, brake: 15000, rarity: 5,
    colors: [0x111111, 0xf77f00, 0x9d0208, 0x3a86ff, 0xffffff, 0x606c38],
  },
  summit: {
    name: 'Summit', body: 'suv', L: 4.9, W: 2.0, H: 1.85, wheelbase: 2.9, track: 1.7, wheelR: 0.4, clearance: 0.34,
    mass: 2150, force: 11500, top: 44, grip: 0.95, drive: 'awd', steer: 0.58, brake: 17000, rarity: 7, camDist: 8.2, camHeight: 1.9,
    colors: [0x222222, 0xe5e5e5, 0x283618, 0x14213d, 0x7f5539, 0x6d6875],
  },
  hauler: {
    name: 'Hauler', body: 'pickup', L: 5.3, W: 2.0, H: 1.8, wheelbase: 3.2, track: 1.7, wheelR: 0.4, clearance: 0.34,
    mass: 2100, force: 11000, top: 42, grip: 0.92, drive: 'rwd', steer: 0.56, brake: 16000, rarity: 6, camDist: 8.5, camHeight: 1.9,
    colors: [0x9b2226, 0x005f73, 0xe9d8a6, 0x3d405b, 0xffffff, 0x495057],
  },
  parcel: {
    name: 'Parcel Van', body: 'van', L: 5.1, W: 2.0, H: 2.2, wheelbase: 3.1, track: 1.72, wheelR: 0.38, clearance: 0.3,
    mass: 2400, force: 9500, top: 37, grip: 0.88, drive: 'rwd', steer: 0.55, brake: 15000, rarity: 4, camDist: 9, camHeight: 2.3,
    colors: [0xffffff, 0xd9d9d9, 0x8d99ae, 0x6a994e, 0xbc4749],
  },
  taxi: {
    name: 'Cab', body: 'sedan', L: 4.8, W: 1.86, H: 1.45, wheelbase: 2.8, track: 1.58, wheelR: 0.34, clearance: 0.26,
    mass: 1450, force: 8600, top: 47, grip: 1.0, drive: 'rwd', steer: 0.62, brake: 15000, rarity: 3, taxi: true,
    colors: [0xf4c20d],
  },
  police: {
    name: 'Police Cruiser', body: 'sedan', L: 4.95, W: 1.9, H: 1.5, wheelbase: 2.9, track: 1.6, wheelR: 0.35, clearance: 0.26,
    mass: 1650, force: 12500, top: 56, grip: 1.08, drive: 'rwd', steer: 0.6, brake: 18000, rarity: 0, police: true,
    colors: [0x111111],
  },
  // the law at three stars and up: Sheriff SUVs on rural roadblocks, the SWAT Enforcer at four
  sheriff: {
    name: 'Sheriff SUV', body: 'suv', L: 4.95, W: 2.0, H: 1.85, wheelbase: 2.9, track: 1.7, wheelR: 0.4, clearance: 0.32,
    mass: 2200, force: 13500, top: 50, grip: 1.0, drive: 'awd', steer: 0.58, brake: 18000, rarity: 0, police: true, livery: 0xe6e1d3, camDist: 8.2, camHeight: 1.9,
    colors: [0xe6e1d3],
  },
  enforcer: {
    name: 'Enforcer', body: 'van', L: 5.5, W: 2.1, H: 2.45, wheelbase: 3.35, track: 1.8, wheelR: 0.42, clearance: 0.34, armored: true,
    mass: 3900, force: 15500, top: 42, grip: 0.92, drive: 'rwd', steer: 0.54, brake: 21000, rarity: 0, police: true, livery: 0x1c2534, camDist: 10, camHeight: 2.6,
    health: 2000, bulletMul: 0.45, colors: [0x1c2534],
  },
  // Gruppe-style cash-in-transit van: bullet resistant; shoot the back doors open (heists.js)
  stockade: {
    name: 'Stockade', body: 'van', L: 5.7, W: 2.15, H: 2.5, wheelbase: 3.45, track: 1.82, wheelR: 0.43, clearance: 0.34, armored: true,
    mass: 4300, force: 15000, top: 36, grip: 0.9, drive: 'rwd', steer: 0.52, brake: 22000, rarity: 0, camDist: 10.5, camHeight: 2.7,
    health: 2600, bulletMul: 0.3, colors: [0xf0f0ec],
  },
  // more on the streets
  buffalo: {
    name: 'Buffalo S', body: 'sedan', L: 4.95, W: 1.92, H: 1.42, wheelbase: 2.95, track: 1.62, wheelR: 0.36, clearance: 0.22,
    mass: 1650, force: 13500, top: 58, grip: 1.08, drive: 'awd', steer: 0.6, brake: 18000, rarity: 4,
    colors: [0x0b0b0d, 0x2f3d4c, 0xb8bcc2, 0x7a0f14, 0xf2f2f2, 0x1f4e3d],
  },
  baller: {
    name: 'Baller', body: 'suv', L: 5.05, W: 2.02, H: 1.82, wheelbase: 3.0, track: 1.72, wheelR: 0.42, clearance: 0.3,
    mass: 2350, force: 13000, top: 50, grip: 0.98, drive: 'awd', steer: 0.57, brake: 18000, rarity: 4, camDist: 8.3, camHeight: 1.9,
    colors: [0x0b0b0d, 0xf4f4f2, 0x3c3f44, 0x5b4a3a, 0x1b2a41],
  },
  tempest: {
    name: 'Tempest', body: 'super', L: 4.75, W: 2.04, H: 1.12, wheelbase: 2.8, track: 1.74, wheelR: 0.36, clearance: 0.13,
    mass: 1450, force: 18500, top: 76, grip: 1.28, drive: 'awd', steer: 0.57, brake: 23000, rarity: 1,
    colors: [0x2dd4bf, 0xff6b00, 0xf8f8f8, 0x7c3aed, 0x111111, 0xd90429],
  },
  zenith: {
    name: 'Zenith', body: 'super', L: 4.6, W: 2.0, H: 1.15, wheelbase: 2.7, track: 1.7, wheelR: 0.35, clearance: 0.14,
    mass: 1400, force: 17000, top: 72, grip: 1.25, drive: 'awd', steer: 0.58, brake: 22000, rarity: 1,
    colors: [0xffd60a, 0xe63946, 0x00b4d8, 0xffffff, 0x111111, 0x80ed99],
  },
  pico: {
    name: 'Pico', body: 'hatch', L: 4.0, W: 1.76, H: 1.5, wheelbase: 2.5, track: 1.5, wheelR: 0.31, clearance: 0.2,
    mass: 1100, force: 6400, top: 43, grip: 1.02, drive: 'fwd', steer: 0.66, brake: 12500, rarity: 7,
    colors: [0xe63946, 0x2a9d8f, 0xf4a261, 0xe9ecef, 0x457b9d, 0x6a4c93, 0x8ac926],
  },
  bouncer: {
    name: 'Bouncer', body: 'lowrider', L: 5.3, W: 1.96, H: 1.35, wheelbase: 3.05, track: 1.6, wheelR: 0.33, clearance: 0.16,
    mass: 1750, force: 9000, top: 46, grip: 0.95, drive: 'rwd', steer: 0.6, brake: 14000, rarity: 3, hydraulics: true,
    colors: [0x5a189a, 0x2a9d8f, 0xe76f51, 0x9d0208, 0x264653, 0xffb703],
  },
  boxer: {
    name: 'Boxer Truck', body: 'truck', L: 7.4, W: 2.35, H: 3.2, wheelbase: 4.4, track: 1.95, wheelR: 0.48, clearance: 0.38,
    mass: 5200, force: 20000, top: 32, grip: 0.85, drive: 'rwd', steer: 0.5, brake: 30000, rarity: 2, camDist: 12, camHeight: 3.2,
    colors: [0xffffff, 0x1d3557, 0x9b2226],
  },
  // ------------------------------------------------ two-wheelers (car physics on a narrow track; see bikes.js)
  razor: {
    name: 'Razor 600', body: 'sport', bike: 'moto', L: 2.1, W: 0.72, H: 1.2, wheelbase: 1.42, track: 0.3, wheelR: 0.31, clearance: 0.14,
    mass: 330, force: 3100, top: 64, grip: 1.15, drive: 'rwd', steer: 0.55, brake: 3900, rarity: 2, camDist: 5.4, camHeight: 1.55, health: 600,
    colors: [0xd00000, 0x111111, 0x0077b6, 0xffffff, 0x2dc653, 0xff9f1c],
  },
  trail: {
    name: 'Trailblazer', body: 'dirt', bike: 'moto', offroad: true, L: 2.15, W: 0.8, H: 1.25, wheelbase: 1.46, track: 0.3, wheelR: 0.36, clearance: 0.3,
    mass: 300, force: 2500, top: 45, grip: 1.05, drive: 'rwd', steer: 0.62, brake: 3300, rarity: 1, camDist: 5.4, camHeight: 1.6, health: 550,
    colors: [0xff6b00, 0xf7d000, 0x1b4dd8, 0x2e7d32, 0xffffff],
  },
  bmx: {
    name: 'BMX', body: 'bmx', bike: 'bicycle', pedal: true, L: 1.65, W: 0.62, H: 1.1, wheelbase: 0.98, track: 0.25, wheelR: 0.3, clearance: 0.2,
    mass: 95, force: 440, top: 10.5, grip: 1.0, drive: 'rwd', steer: 0.75, brake: 900, rarity: 0, camDist: 4.6, camHeight: 1.45, health: 300,
    colors: [0x00b4d8, 0xef233c, 0x111111, 0xffd60a, 0x80ed99],
  },
  roadbike: {
    name: 'Road Bike', body: 'road', bike: 'bicycle', pedal: true, L: 1.75, W: 0.6, H: 1.1, wheelbase: 1.02, track: 0.25, wheelR: 0.34, clearance: 0.22,
    mass: 100, force: 480, top: 14, grip: 1.0, drive: 'rwd', steer: 0.62, brake: 950, rarity: 0, camDist: 4.8, camHeight: 1.5, health: 300,
    colors: [0xe63946, 0x1d3557, 0xf1faee, 0x2a9d8f, 0x111111],
  },
  skateboard: {
    name: 'Skateboard', body: 'board', bike: 'board', board: true, L: 0.82, W: 0.22, H: 0.14, wheelbase: 0.48, track: 0.19, wheelR: 0.028, clearance: 0.06,
    mass: 80, force: 300, top: 9.5, grip: 0.9, drive: 'rwd', steer: 0.42, brake: 380, rarity: 0, camDist: 4.2, camHeight: 1.75, health: 250,
    colors: [0x1d3557, 0xe63946, 0xffb703, 0x2a9d8f, 0x8338ec, 0x111111, 0xf1faee],
  },
  // ------------------------------------------------ military ground vehicles (car physics)
  ranger: {
    name: 'Ranger', body: 'suv', L: 4.7, W: 2.05, H: 1.95, wheelbase: 2.85, track: 1.75, wheelR: 0.44, clearance: 0.42,
    mass: 2300, force: 13000, top: 42, grip: 1.05, drive: 'awd', steer: 0.6, brake: 18000, rarity: 0, camDist: 8.4, camHeight: 2.0, military: true,
    colors: [0x4b5320, 0x5a5a3c, 0x6b5b3e],
  },
  barracks: {
    name: 'Barracks', body: 'truck', L: 8.2, W: 2.5, H: 3.3, wheelbase: 4.8, track: 2.0, wheelR: 0.55, clearance: 0.5,
    mass: 7800, force: 26000, top: 30, grip: 0.9, drive: 'awd', steer: 0.5, brake: 36000, rarity: 0, camDist: 13, camHeight: 3.4, military: true,
    colors: [0x4b5320],
  },
  // ------------------------------------------------ boats (see boat.js)
  dinghy: {
    name: 'Dinghy', kind: 'boat', boat: 'rib', L: 4.4, W: 2.0, H: 1.1, draft: 0.28, freeboard: 0.55, mass: 520, force: 4200, top: 17, turn: 1.25,
    rarity: 0, camDist: 7.5, camHeight: 2.4, health: 600, wheelbase: 2.6, track: 1.6, wheelR: 0.3, clearance: 0, grip: 1, drive: 'rwd', steer: 0.5, brake: 1,
    colors: [0xd62828, 0x2b2d42, 0xf77f00, 0x3a5a40],
  },
  speedboat: {
    name: 'Squalo', kind: 'boat', boat: 'speed', L: 6.8, W: 2.4, H: 1.5, draft: 0.42, freeboard: 0.9, mass: 1500, force: 15500, top: 32, turn: 1.05,
    rarity: 0, camDist: 9.5, camHeight: 2.8, health: 900, wheelbase: 4, track: 2, wheelR: 0.3, clearance: 0, grip: 1, drive: 'rwd', steer: 0.5, brake: 1,
    colors: [0xffffff, 0xe63946, 0x1d3557, 0xffb703, 0x111111],
  },
  jetski: {
    name: 'Wave Rider', kind: 'boat', boat: 'jetski', astride: true, L: 3.1, W: 1.15, H: 1.1, draft: 0.2, freeboard: 0.42, mass: 380, force: 4600, top: 25, turn: 1.9,
    rarity: 0, camDist: 5.8, camHeight: 2.0, health: 450, wheelbase: 1.8, track: 0.8, wheelR: 0.2, clearance: 0, grip: 1, drive: 'rwd', steer: 0.5, brake: 1,
    colors: [0xffd60a, 0x00b4d8, 0xe63946, 0x80ed99, 0xffffff],
  },
  cruiser: {
    name: 'Marquis', kind: 'boat', boat: 'cruiser', L: 11.5, W: 3.8, H: 3.6, draft: 0.9, freeboard: 1.35, mass: 9500, force: 30000, top: 14, turn: 0.5,
    rarity: 0, camDist: 17, camHeight: 5.5, health: 1600, wheelbase: 7, track: 3, wheelR: 0.3, clearance: 0, grip: 1, drive: 'rwd', steer: 0.5, brake: 1,
    colors: [0xffffff, 0xf1faee, 0x264653],
  },
  policeboat: {
    name: 'Predator', kind: 'boat', boat: 'police', police: true, weapons: true, L: 8.4, W: 2.8, H: 2.7, draft: 0.5, freeboard: 1.0, mass: 2900, force: 24000, top: 30, turn: 0.95,
    rarity: 0, camDist: 11, camHeight: 3.6, health: 1400, wheelbase: 5, track: 2.2, wheelR: 0.3, clearance: 0, grip: 1, drive: 'rwd', steer: 0.5, brake: 1,
    colors: [0xf4f4f4],
  },
  // ------------------------------------------------ tank
  mammoth: {
    name: 'Mammoth Tank', kind: 'tank', tank: true, L: 9.2, W: 3.7, H: 2.9, mass: 46000, top: 13, turn: 0.85, accel: 3.2,
    wheelbase: 5, track: 3, wheelR: 0.5, clearance: 0.5, force: 1, grip: 1, drive: 'awd', steer: 0.5, brake: 1, rarity: 0,
    camDist: 14, camHeight: 4.2, military: true, weapons: true, health: 3000, bulletMul: 0.05, blastMul: 0.35, colors: [0x4f5a36],
  },
  // ------------------------------------------------ Sol Line train (locomotive; carriages follow)
  train: {
    name: 'Sol Line Express', kind: 'train', train: true, L: 17, W: 3, H: 4.5, mass: 110000, top: 32, wheelbase: 12, track: 1.4, wheelR: 0.46, clearance: 0.9,
    force: 1, grip: 1, drive: 'awd', steer: 0, brake: 1, rarity: 0, camDist: 24, camHeight: 6, colors: [0x1d4e89, 0x8c1c13, 0x2a6041],
  },
  freight: {
    name: 'Sol Line Freight', kind: 'train', train: true, freight: true, L: 17, W: 3, H: 4.5, mass: 130000, top: 26, wheelbase: 12, track: 1.4, wheelR: 0.46, clearance: 0.9,
    force: 1, grip: 1, drive: 'awd', steer: 0, brake: 1, rarity: 0, camDist: 26, camHeight: 6.5, colors: [0xc8541a, 0x2b2d30, 0x9b1c1c],
  },
  // ------------------------------------------------ aircraft
  skipper: {
    name: 'Skipper', kind: 'plane', aircraft: true, L: 8.4, W: 11, H: 2.9, colW: 1.3, mass: 1100, rarity: 0, health: 700,
    thrust: 11, vStall: 20, vMax: 60, vRotate: 24, pitchRate: 1.3, rollRate: 2.2, yawRate: 0.6, gearH: 1.3, drag: 0.0028,
    camDist: 17, camHeight: 3.4, maxDial: 200, colors: [0xd62828, 0xf4f1de, 0x1d3557, 0xf77f00],
  },
  raptor: {
    name: 'Raptor', kind: 'jet', aircraft: true, L: 16.5, W: 11.5, H: 4.4, colW: 3.2, mass: 12000, rarity: 0, weapons: true, military: true, health: 1100,
    thrust: 28, vStall: 34, vMax: 150, vRotate: 42, pitchRate: 1.6, rollRate: 3.6, yawRate: 0.55, gearH: 1.9, drag: 0.0011,
    camDist: 25, camHeight: 5, maxDial: 400, colors: [0x6b7178, 0x4d535a],
  },
  hercules: {
    name: 'Hercules', kind: 'plane', aircraft: true, L: 29, W: 40, H: 11, colW: 4.4, mass: 40000, rarity: 0, cargo: true, military: true, health: 1800,
    thrust: 7.5, vStall: 30, vMax: 82, vRotate: 36, pitchRate: 0.55, rollRate: 0.75, yawRate: 0.3, gearH: 2.1, drag: 0.0009,
    camDist: 46, camHeight: 12, maxDial: 250, colors: [0x5d6a4f],
  },
  warhawk: {
    name: 'Warhawk', kind: 'heli', aircraft: true, L: 15.5, W: 3.2, H: 4.2, colW: 2.2, mass: 7000, rarity: 0, weapons: true, military: true, health: 1300, bulletMul: 0.6,
    vMax: 72, rotorR: 7.3, skidH: 0.25, camDist: 18, camHeight: 5.5, maxDial: 200, colors: [0x3d4a33],
  },
  skylark: {
    name: 'Skylark', kind: 'heli', aircraft: true, L: 11.5, W: 2.4, H: 3.3, colW: 1.9, mass: 2400, rarity: 0, health: 800,
    vMax: 62, rotorR: 5.4, skidH: 0.25, camDist: 14, camHeight: 4.2, maxDial: 200, colors: [0x1d3557, 0xe63946, 0xf1faee, 0x111111],
  },
};

export const TRAFFIC_POOL = Object.entries(VEHICLES).filter(([, d]) => d.rarity > 0).map(([id, d]) => [id, d.rarity]);
export const BIKES = Object.keys(VEHICLES).filter((id) => VEHICLES[id].bike && !VEHICLES[id].board);
export const BOATS = Object.keys(VEHICLES).filter((id) => VEHICLES[id].kind === 'boat');
