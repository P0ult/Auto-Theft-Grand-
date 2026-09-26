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
};

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
    this.gp = { lx: 0, ly: 0, rx: 0, ry: 0, lt: 0, rt: 0, buttons: [], prev: [] };
    this.lastDevice = 'kbm';
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
    window.addEventListener('gamepadconnected', (e) => { this.gamepad = e.gamepad.index; });
    window.addEventListener('gamepaddisconnected', () => { this.gamepad = null; });
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
    dx += this.gp.rx * 0.05 * this.sensitivity; dy += this.gp.ry * 0.04 * this.sensitivity;
    if (this.invertY) dy = -dy;
    return [dx, dy];
  }
  fireDown() { return this.mouse.left || this.gp.rt > 0.5 && this.aimDown(); }
  firePressed() { return this.mouse.leftPressed || this._gpPressedIdx(7) && this.aimDown(); }
  aimDown() { return this.mouse.right || this.gp.lt > 0.4; }
  attackPressed() { return this.mouse.leftPressed || this._gpPressedIdx(2); }

  _gpMap(action) {
    // standard mapping indices
    switch (action) {
      case 'jump': return 3;        // Y / triangle... (A used for sprint in SA)
      case 'sprint': return 0;      // A
      case 'handbrake': return 5;   // RB
      case 'enter': return 3;       // Y
      case 'reload': return 1;
      case 'nextWeapon': return 15;
      case 'prevWeapon': return 14;
      case 'crouch': return 10;
      case 'horn': return 11;
      case 'radio': return 12;
      case 'camera': return 8;
      case 'map': return 8;
      case 'pause': return 9;
      case 'skip': return 0;
      case 'lookBehind': return 4;
      case 'hydraulics': return 13;
      default: return -1;
    }
  }
  _gpDown(action) { const i = this._gpMap(action); return i >= 0 && !!this.gp.buttons[i]; }
  _gpHit(action) { const i = this._gpMap(action); return i >= 0 && this._gpPressedIdx(i); }
  _gpPressedIdx(i) { return !!this.gp.buttons[i] && !this.gp.prev[i]; }

  pollGamepad() {
    this.gp.prev = this.gp.buttons.slice();
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    let pad = null;
    if (this.gamepad != null) pad = pads[this.gamepad];
    if (!pad) for (const p of pads) if (p) { pad = p; break; }
    if (!pad) { this.gp.lx = this.gp.ly = this.gp.rx = this.gp.ry = this.gp.lt = this.gp.rt = 0; this.gp.buttons = []; return; }
    const dz = (v) => (Math.abs(v) < 0.15 ? 0 : (v - Math.sign(v) * 0.15) / 0.85);
    this.gp.lx = dz(pad.axes[0] || 0); this.gp.ly = dz(pad.axes[1] || 0);
    this.gp.rx = dz(pad.axes[2] || 0); this.gp.ry = dz(pad.axes[3] || 0);
    this.gp.lt = pad.buttons[6] ? pad.buttons[6].value : 0;
    this.gp.rt = pad.buttons[7] ? pad.buttons[7].value : 0;
    this.gp.buttons = pad.buttons.map((b) => b.pressed);
    if (this.gp.buttons.some((b) => b) || Math.abs(this.gp.lx) + Math.abs(this.gp.rx) > 0.2) this.lastDevice = 'gamepad';
  }

  endFrame() {
    this.pressed.clear();
    this.released.clear();
    this.mouse.dx = this.mouse.dy = 0;
    this.mouse.leftPressed = this.mouse.rightPressed = false;
    this.mouse.wheel = 0;
  }
}
