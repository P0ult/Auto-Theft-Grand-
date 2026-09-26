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
  zenith: {
    name: 'Zenith', body: 'super', L: 4.6, W: 2.0, H: 1.15, wheelbase: 2.7, track: 1.7, wheelR: 0.35, clearance: 0.14,
    mass: 1400, force: 17000, top: 72, grip: 1.25, drive: 'awd', steer: 0.58, brake: 22000, rarity: 1,
    colors: [0xffd60a, 0xe63946, 0x00b4d8, 0xffffff, 0x111111, 0x80ed99],
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
