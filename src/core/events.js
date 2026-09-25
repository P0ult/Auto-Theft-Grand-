// Minimal event emitter.
export class Events {
  constructor() { this.map = new Map(); }
  on(name, fn) { if (!this.map.has(name)) this.map.set(name, new Set()); this.map.get(name).add(fn); return () => this.off(name, fn); }
  off(name, fn) { this.map.get(name)?.delete(fn); }
  emit(name, ...args) { const s = this.map.get(name); if (s) for (const fn of [...s]) fn(...args); }
}
