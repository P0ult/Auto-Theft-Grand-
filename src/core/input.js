// Keyboard / mouse / gamepad input with pointer lock and action mapping.

const BINDINGS = {
  forward: ['KeyW', 'ArrowUp'],
  back: ['KeyS', 'ArrowDown'],
  left: ['KeyA', 'ArrowLeft'],
  right: ['KeyD', 'ArrowRight'],
  sprint: ['ShiftLeft', 'ShiftRight'],
  jump: ['Space'],
  handbrake: ['Space'],
  enter: ['KeyF', 'Enter'],
  reload: ['KeyR'],
  nextWeapon: ['KeyE'],
  prevWeapon: ['KeyQ'],
  crouch: ['KeyC', 'ControlLeft'],
  horn: ['KeyH'],
  radio: ['KeyN'],
  camera: ['KeyV'],
  map: ['KeyM'],
  pause: ['Escape', 'KeyP'],
  skip: ['Space', 'Enter'],
  lookBehind: ['KeyB'],
  hydraulics: ['KeyG'],
  shop: ['KeyY'],
  teleport: ['KeyT'],
  hail: ['KeyH'],
  passenger: ['KeyG'],
  taxiJob: ['KeyJ'],
  skipTrip: ['Space'],
  chat: ['Slash'],
  console: ['Backquote'],
  pet: ['KeyK'],
  nitro: ['ShiftLeft', 'ShiftRight'],
};

// Gamepad buttons in the W3C "standard" layout, named the Xbox way (PlayStation: A=✕ B=○ X=□ Y=△,
// LB/RB=L1/R1, LT/RT=L2/R2, View=Share/Create, Menu=Options). Pads the browser doesn't map are remapped onto it.
export const GP = { A: 0, B: 1, X: 2, Y: 3, LB: 4, RB: 5, LT: 6, RT: 7, VIEW: 8, MENU: 9, LS: 10, RS: 11, UP: 12, DOWN: 13, LEFT: 14, RIGHT: 15, HOME: 16 };

// action -> [button on foot, button in a vehicle] (GTA V-style layout; the triggers are handled separately:
// RT fire / LT aim on foot, RT gas / LT brake in a vehicle)
const GP_BIND = {
  sprint: [GP.A, null],
  jump: [GP.X, null],
  reload: [GP.B, null],
  enter: [GP.Y, GP.Y],
  prevWeapon: [null, null], // (LB: a tap flicks back, holding opens the weapon wheel; see weaponwheel.js)
  nextWeapon: [GP.RB, null],
  crouch: [GP.LS, null],
  handbrake: [null, GP.RB],
  horn: [null, GP.LS],
  lookBehind: [null, GP.RS],
  camera: [null, GP.VIEW],
  radio: [null, GP.RIGHT],
  hydraulics: [null, GP.UP],
  taxiJob: [null, GP.LEFT],
  nitro: [null, GP.A],
  teleport: [null, null], // (D-pad up takes the phone out; see phone.js)
  hail: [GP.RIGHT, null],
  passenger: [GP.LEFT, null],
  map: [GP.DOWN, GP.DOWN],
  pause: [GP.MENU, GP.MENU],
  skip: [GP.A, GP.A],
  skipTrip: [null, GP.A],
  pet: [GP.RS, null],
};

// which glyphs to show for a pad
function padFamily(id = '') {
  const s = id.toLowerCase();
  if (/045e|xbox|xinput/.test(s)) return 'xbox';
  if (/054c|playstation|dualshock|dualsense|ps[345]|^wireless controller/.test(s)) return 'playstation';
  if (/046d|logitech/.test(s)) return 'logitech';
  return 'xbox';
}

export class Input {
  constructor(canvas) {
    this.canvas = canvas;
    this.keys = new Set();
    this.pressed = new Set();   // pressed this frame
    this.released = new Set();
    this.mouse = { dx: 0, dy: 0, left: false, right: false, leftPressed: false, rightPressed: false, wheel: 0 };
    this.locked = false;
    this.enabled = true;
    this.sensitivity = 1;
    this.invertY = false;
    this.gamepad = null;
    this.gp = { lx: 0, ly: 0, rx: 0, ry: 0, lt: 0, rt: 0, ltPrev: 0, rtPrev: 0, buttons: [], prev: [], connected: false, id: '', family: 'xbox', standard: true };
    this.inVehicle = false; // set by the game each frame: picks the on-foot or in-vehicle pad bindings
    this.frameDt = 1 / 60;
    this.lastDevice = 'kbm';
    this.mask = new Set(); // pad buttons something else (the phone) has taken this frame
    this.onPointerLockChange = null;

    window.addEventListener('keydown', (e) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return; // typing (chat, names)
      if (e.repeat) { if (this.enabled && !e.metaKey) this._maybePrevent(e); return; }
      this.keys.add(e.code);
      this.pressed.add(e.code);
      this.lastDevice = 'kbm';
      this._maybePrevent(e);
    });
    window.addEventListener('keyup', (e) => { this.keys.delete(e.code); this.released.add(e.code); });
    window.addEventListener('blur', () => { this.keys.clear(); this.mouse.left = this.mouse.right = false; });
    canvas.addEventListener('mousedown', (e) => {
      if (!this.locked && this.enabled && this.wantLock) this.requestLock();
      if (e.button === 0) { this.mouse.left = true; this.mouse.leftPressed = true; }
      if (e.button === 2) { this.mouse.right = true; this.mouse.rightPressed = true; }
      this.lastDevice = 'kbm';
    });
    window.addEventListener('mouseup', (e) => {
      if (e.button === 0) this.mouse.left = false;
      if (e.button === 2) this.mouse.right = false;
    });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());
    window.addEventListener('mousemove', (e) => {
      if (this.locked) { this.mouse.dx += e.movementX; this.mouse.dy += e.movementY; }
      else if (this.lockFailed && this.enabled && e.target === canvas) {
        // fallback when pointer lock is unavailable (e.g. sandboxed iframes): look by moving the mouse
        this.mouse.dx += e.movementX * 1.4; this.mouse.dy += e.movementY * 1.4;
      }
    });
    document.addEventListener('pointerlockerror', () => { this.lockFailed = true; });
    window.addEventListener('wheel', (e) => { this.mouse.wheel += Math.sign(e.deltaY); }, { passive: true });
    document.addEventListener('pointerlockchange', () => {
      this.locked = document.pointerLockElement === canvas;
      if (this.onPointerLockChange) this.onPointerLockChange(this.locked);
    });
    window.addEventListener('gamepadconnected', (e) => { this.gamepad = e.gamepad.index; this.onGamepad?.(true, e.gamepad); });
    window.addEventListener('gamepaddisconnected', (e) => { if (this.gamepad === e.gamepad.index) this.gamepad = null; this.onGamepad?.(false, e.gamepad); });
    this.wantLock = false;
  }

  _maybePrevent(e) {
    if (['Space', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Tab'].includes(e.code)) e.preventDefault();
  }

  requestLock() {
    if (document.pointerLockElement !== this.canvas) {
      try {
        const p = this.canvas.requestPointerLock({ unadjustedMovement: true });
        if (p && p.catch) p.catch(() => { try { const p2 = this.canvas.requestPointerLock(); if (p2 && p2.catch) p2.catch(() => { this.lockFailed = true; }); } catch { this.lockFailed = true; } });
      } catch { this.lockFailed = true; }
    }
  }
  exitLock() { if (document.pointerLockElement) document.exitPointerLock(); }

  down(action) {
    const codes = BINDINGS[action];
    if (codes) for (const c of codes) if (this.keys.has(c)) return true;
    return this._gpDown(action);
  }
  hit(action) {
    const codes = BINDINGS[action];
    if (codes) for (const c of codes) if (this.pressed.has(c)) return true;
    return this._gpHit(action);
  }
  key(code) { return this.keys.has(code); }
  keyHit(code) { return this.pressed.has(code); }

  // Analog axes combining keyboard + gamepad
  moveX() { let v = (this.down('right') ? 1 : 0) - (this.down('left') ? 1 : 0); if (Math.abs(this.gp.lx) > Math.abs(v)) v = this.gp.lx; return v; }
  moveY() { let v = (this.down('forward') ? 1 : 0) - (this.down('back') ? 1 : 0); if (Math.abs(this.gp.ly) > Math.abs(v)) v = -this.gp.ly; return v; }
  throttle() { return Math.max(this.down('forward') ? 1 : 0, this.gp.rt); }
  brake() { return Math.max(this.down('back') ? 1 : 0, this.gp.lt); }
  steer() { let v = (this.down('left') ? 1 : 0) - (this.down('right') ? 1 : 0); if (Math.abs(this.gp.lx) > 0.05) v = -this.gp.lx; return v; }
  lookDelta() {
    const s = 0.0022 * this.sensitivity;
    let dx = this.mouse.dx * s, dy = this.mouse.dy * s;
    // right stick: a rate (rad/s) with a curve for fine aim, slower while aiming down the sights
    const g = this.gp, dt = Math.min(this.frameDt, 0.1);
    if (g.rx || g.ry) {
      const m = Math.hypot(g.rx, g.ry), k = Math.pow(Math.min(1, m), 1.6) / (m || 1);
      const rate = (this.inVehicle ? 2.6 : this.aimDown() ? 1.7 : 3.3) * this.sensitivity;
      dx += g.rx * k * rate * dt; dy += g.ry * k * rate * 0.8 * dt;
    }
    if (this.invertY) dy = -dy;
    return [dx, dy];
  }
  // on foot: RT fires (hip fire, or aimed with LT held), LT aims
  fireDown() { return this.mouse.left || (!this.inVehicle && this.gp.rt > 0.45); }
  firePressed() { return this.mouse.leftPressed || (!this.inVehicle && this.gp.rt > 0.45 && this.gp.rtPrev <= 0.45); }
  aimDown() { return this.mouse.right || (this.inVehicle ? !!this.gp.buttons[GP.LB] : this.gp.lt > 0.4); }
  attackPressed() { return this.firePressed(); }
  // in a vehicle: hold LB to aim a drive-by, RB to shoot (instead of the handbrake while LB is held)
  driveByFire() { return this.mouse.left || (!!this.gp.buttons[GP.LB] && !!this.gp.buttons[GP.RB]); }
  // armed aircraft & tanks: RB guns (hold), LB missiles / rockets
  vehFire() { return this.mouse.left || !!this.gp.buttons[GP.RB]; }
  vehAltPressed() { return this.mouse.rightPressed || this._gpPressedIdx(GP.LB); }
  gpDown(i) { return !!this.gp.buttons[i] && !this.mask.has(i); }
  gpHit(i) { return this._gpPressedIdx(i); }

  _gpMap(action) {
    const b = GP_BIND[action];
    return b ? (b[this.inVehicle ? 1 : 0] ?? -1) : -1;
  }
  _gpDown(action) {
    const i = this._gpMap(action);
    if (action === 'handbrake' && this.gp.buttons[GP.LB]) return false; // LB+RB is a drive-by
    return i >= 0 && !!this.gp.buttons[i] && !this.mask.has(i);
  }
  _gpHit(action) { const i = this._gpMap(action); return i >= 0 && this._gpPressedIdx(i); }
  _gpPressedIdx(i) { return !!this.gp.buttons[i] && !this.gp.prev[i] && !this.mask.has(i); }

  pollGamepad() {
    const g = this.gp;
    g.prev = g.buttons; g.ltPrev = g.lt; g.rtPrev = g.rt;
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    let pad = null;
    if (this.gamepad != null) pad = pads[this.gamepad];
    if (!pad || !pad.connected) { pad = null; for (const p of pads) if (p && p.connected && p.buttons.length >= 10) { pad = p; break; } }
    if (!pad) { g.lx = g.ly = g.rx = g.ry = g.lt = g.rt = 0; g.buttons = []; g.connected = false; return; }
    g.connected = true;
    if (g.id !== pad.id) { g.id = pad.id; g.family = padFamily(pad.id); }
    g.standard = pad.mapping === 'standard';
    const B = (i) => pad.buttons[i] || null, ax = (i) => pad.axes[i] || 0;
    let btn, lx, ly, rx, ry, lt, rt;
    if (g.standard) {
      btn = pad.buttons.map((b) => b.pressed);
      lx = ax(0); ly = ax(1); rx = ax(2); ry = ax(3);
      lt = B(6) ? B(6).value : 0; rt = B(7) ? B(7).value : 0;
    } else if (/045e|xbox|x-box|xinput/i.test(pad.id)) {
      // raw XInput (e.g. Firefox on Linux): A B X Y LB RB View Menu Guide LS RS; triggers & d-pad on axes
      const r = pad.buttons.map((b) => b.pressed);
      btn = [r[0], r[1], r[2], r[3], r[4], r[5], false, false, r[6], r[7], r[9], r[10], false, false, false, false, r[8]];
      lx = ax(0); ly = ax(1); rx = ax(3); ry = ax(4);
      lt = (ax(2) + 1) / 2; rt = (ax(5) + 1) / 2;
      if (pad.axes.length >= 8) { btn[12] = ax(7) < -0.5; btn[13] = ax(7) > 0.5; btn[14] = ax(6) < -0.5; btn[15] = ax(6) > 0.5; }
    } else {
      // DirectInput pads (Logitech in "D" mode, PlayStation pads the browser doesn't know):
      // □/X ✕/A ○/B △/Y L1 R1 L2 R2 Select Start L3 R3
      const r = pad.buttons.map((b) => b.pressed), v = (i) => (pad.buttons[i] ? pad.buttons[i].value : 0);
      btn = [r[1], r[2], r[0], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15], r[16]];
      lx = ax(0); ly = ax(1); rx = ax(2); ry = ax(pad.axes.length >= 6 && /054c/i.test(pad.id) ? 5 : 3);
      lt = v(6); rt = v(7);
      const hat = pad.axes.length >= 10 ? ax(9) : 9;
      if (Math.abs(hat) <= 1.01) {
        // 8-way hat switch: -1 = up, going clockwise in steps of 2/7
        const k = Math.round((hat + 1) * 3.5) % 8;
        btn[12] = k === 7 || k <= 1; btn[14] = k >= 5; btn[15] = k >= 1 && k <= 3; btn[13] = k >= 3 && k <= 5;
      }
    }
    // radial dead zone on the sticks
    const stick = (x, y) => { const m = Math.hypot(x, y); if (m < 0.16) return [0, 0]; const s = Math.min(1, (m - 0.16) / 0.84) / m; return [x * s, y * s]; };
    [g.lx, g.ly] = stick(lx, ly);
    [g.rx, g.ry] = stick(rx, ry);
    g.lt = lt < 0.06 ? 0 : Math.min(1, lt); g.rt = rt < 0.06 ? 0 : Math.min(1, rt);
    btn[GP.LT] = btn[GP.LT] || g.lt > 0.4; btn[GP.RT] = btn[GP.RT] || g.rt > 0.4;
    g.buttons = btn.map((b) => !!b);
    if (g.buttons.some((b) => b) || Math.abs(g.lx) + Math.abs(g.ly) + Math.abs(g.rx) + Math.abs(g.ry) > 0.2 || g.lt + g.rt > 0.2) this.lastDevice = 'gamepad';
  }

  endFrame() {
    this.mask.clear();
    this.pressed.clear();
    this.released.clear();
    this.mouse.dx = this.mouse.dy = 0;
    this.mouse.leftPressed = this.mouse.rightPressed = false;
    this.mouse.wheel = 0;
  }
}
