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
};

export const TRAFFIC_POOL = Object.entries(VEHICLES).filter(([, d]) => d.rarity > 0).map(([id, d]) => [id, d.rarity]);
