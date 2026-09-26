// Two ways to share a world. Both carry one state object per player (sent whole a dozen times a second):
//  - RoomTransport: the claude.ai artifact's `room` capability (everyone with the page open; a room code
//    picks a named room inside it). State travels as the player's presence.
//  - WsTransport: the WebSocket relay built into server.mjs, for local / self-hosted play.
// Both call onPeer(id, state), onLeft(id) and onStatus(connected).

const ROOM_PREFIX = 'atg-';

export function cleanCode(code) { return String(code || '').toLowerCase().replace(/[^a-z0-9]/g, '').slice(0, 12); }

// ------------------------------------------------------------------ claude.ai room
export class RoomTransport {
  constructor(lobby) {
    this.lobby = lobby;
    this.r = null;
    this.kind = 'room';
    this.label = 'claude.ai live room';
    this.unsubs = [];
    this.onPeer = null; this.onLeft = null; this.onStatus = null;
  }

  static async probe(timeoutMs = 10000) {
    const c = window.claude;
    if (!c || typeof c.use !== 'function') return null;
    try {
      const room = await Promise.race([c.use('room'), new Promise((r) => setTimeout(() => r(null), timeoutMs))]);
      return room ? new RoomTransport(room) : null;
    } catch { return null; }
  }

  async join(code) {
    await this.leave();
    const name = cleanCode(code);
    this.r = name ? await this.lobby.join(ROOM_PREFIX + name) : this.lobby;
    const r = this.r;
    const seen = (p) => p && !p.sameTab && p.kind === 'viewer' && p.presence && typeof p.presence.g === 'object' && p.presence.g;
    this.unsubs.push(r.onPeers((ch) => {
      if (this.r !== r) return;
      for (const p of [...ch.joined, ...ch.updated]) { if (seen(p)) this.onPeer?.(p.peer, p.presence.g, p.guest); else if (!p.sameTab) this.onLeft?.(p.peer); }
      for (const p of ch.left) if (!p.sameTab) this.onLeft?.(p.peer);
    }, () => this.onStatus?.(false)));
    this.unsubs.push(r.onConnection((ok) => { if (this.r === r) this.onStatus?.(ok); }, () => this.onStatus?.(false)));
    // everyone already here
    for (const p of r.peers()) if (seen(p)) this.onPeer?.(p.peer, p.presence.g, p.guest);
    return true;
  }

  send(state) {
    if (!this.r) return;
    this.r.presence({ g: state }).catch(() => {});
  }

  async leave() {
    for (const u of this.unsubs) { try { u(); } catch { /* already gone */ } }
    this.unsubs = [];
    const r = this.r;
    this.r = null;
    if (!r) return;
    try { if (r === this.lobby) await r.presence({ g: null }); else await r.leave(); } catch { /* ignore */ }
  }

  get connected() { try { return !!this.r && this.r.connected(); } catch { return false; } }
}

// ------------------------------------------------------------------ self-hosted WebSocket relay
export class WsTransport {
  constructor(url) {
    this.url = url;
    this.ws = null;
    this.kind = 'ws';
    this.label = 'local server';
    this.id = null;
    this.room = null;
    this.onPeer = null; this.onLeft = null; this.onStatus = null;
    this._closing = false;
  }

  static async probe() {
    if (!/^https?:$/.test(location.protocol) || typeof WebSocket === 'undefined') return null;
    try {
      const ctl = new AbortController();
      const t = setTimeout(() => ctl.abort(), 2500);
      const res = await fetch('/mp/status', { signal: ctl.signal, cache: 'no-store' });
      clearTimeout(t);
      if (!res.ok) return null;
      const j = await res.json();
      if (!j?.ok) return null;
      return new WsTransport(location.origin.replace(/^http/, 'ws') + '/mp');
    } catch { return null; }
  }

  _open() {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(this.url);
      this.ws = ws;
      const fail = () => reject(new Error('Could not reach the multiplayer server'));
      ws.addEventListener('open', () => resolve(ws), { once: true });
      ws.addEventListener('error', fail, { once: true });
      ws.addEventListener('message', (e) => this._msg(e.data));
      ws.addEventListener('close', () => { if (this.ws === ws) { this.ws = null; this.onStatus?.(false); if (!this._closing && this.room != null) this._reconnect(); } });
    });
  }

  async _reconnect() {
    await new Promise((r) => setTimeout(r, 2000));
    if (this._closing || this.ws) return;
    try { await this._open(); this.ws.send(JSON.stringify({ t: 'join', room: this.room || 'public' })); } catch { this._reconnect(); }
  }

  _msg(text) {
    let m;
    try { m = JSON.parse(text); } catch { return; }
    if (m.t === 'hello') {
      this.id = m.id;
      this._hello?.(true);
      this.onStatus?.(true);
      for (const p of m.peers || []) if (p.d && p.d.g) this.onPeer?.(p.id, p.d.g, false);
    } else if (m.t === 'full') this._hello?.(false, 'That room is full (16 players).');
    else if (m.t === 's' && m.d) {
      if (m.d.g === null) this.onLeft?.(m.id);
      else if (m.d.g) this.onPeer?.(m.id, m.d.g, false);
    } else if (m.t === 'left') this.onLeft?.(m.id);
  }

  async join(code) {
    this._closing = false;
    if (!this.ws || this.ws.readyState !== 1) await this._open();
    this.room = cleanCode(code);
    const ok = await new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(new Error('The multiplayer server did not answer')), 5000);
      this._hello = (good, why) => { clearTimeout(t); this._hello = null; if (good) resolve(true); else reject(new Error(why)); };
      this.ws.send(JSON.stringify({ t: 'join', room: this.room || 'public' }));
    });
    return ok;
  }

  send(state) {
    if (this.ws?.readyState === 1) this.ws.send(JSON.stringify({ t: 's', d: { g: state } }));
  }

  async leave() {
    this._closing = true;
    this.room = null;
    const ws = this.ws;
    this.ws = null;
    if (ws) { try { ws.send(JSON.stringify({ t: 'leave' })); ws.close(); } catch { /* gone */ } }
  }

  get connected() { return this.ws?.readyState === 1; }
}
