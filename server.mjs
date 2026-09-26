// Zero-dependency server for local play: `node server.mjs` then open http://localhost:8080
// Serves the game's files, and relays multiplayer state between browsers over WebSockets (/mp): every
// player sends a small state object (position, animation, vehicle, recent shots / hits / chat) a dozen
// times a second, and the server passes it on to everyone else in the same room.
import { createServer } from 'node:http';
import { createHash, randomBytes } from 'node:crypto';
import { readFile, stat } from 'node:fs/promises';
import { extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('.', import.meta.url));
const port = Number(process.env.PORT) || 8080;
const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.mp3': 'audio/mpeg',
  '.md': 'text/markdown; charset=utf-8',
};

const server = createServer(async (req, res) => {
  try {
    let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (path === '/mp/status') { res.writeHead(200, { 'Content-Type': 'application/json' }).end(JSON.stringify({ ok: true, rooms: rooms.size, players: clients.size })); return; }
    if (path.endsWith('/')) path += 'index.html';
    const file = normalize(join(root, path));
    if (!file.startsWith(root)) { res.writeHead(403).end(); return; }
    const info = await stat(file);
    if (!info.isFile()) throw new Error('not a file');
    const body = await readFile(file);
    res.writeHead(200, { 'Content-Type': types[extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
    res.end(body);
  } catch {
    res.writeHead(404, { 'Content-Type': 'text/plain' }).end('Not found');
  }
});

// ------------------------------------------------------------------ multiplayer relay
const ROOM_MAX = 16;          // players per room
const MSG_MAX = 8 * 1024;     // bytes per message
const RATE_MAX = 40;          // messages per second per player
const rooms = new Map();      // name -> Set<client>
const clients = new Set();
let nextId = 1;

server.on('upgrade', (req, socket) => {
  const url = new URL(req.url, 'http://x');
  const key = req.headers['sec-websocket-key'];
  if (url.pathname !== '/mp' || !key || req.headers.upgrade?.toLowerCase() !== 'websocket') { socket.destroy(); return; }
  const accept = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' + `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  socket.setNoDelay(true);
  const c = { id: `p${nextId++}${randomBytes(3).toString('hex')}`, socket, room: null, state: {}, buf: Buffer.alloc(0), rate: 0, rateT: Date.now(), alive: true };
  clients.add(c);
  socket.on('data', (d) => { c.buf = Buffer.concat([c.buf, d]); readFrames(c); });
  socket.on('close', () => drop(c));
  socket.on('error', () => drop(c));
});

function readFrames(c) {
  for (;;) {
    const b = c.buf;
    if (b.length < 2) return;
    const op = b[0] & 0x0f, masked = b[1] & 0x80;
    let len = b[1] & 0x7f, off = 2;
    if (len === 126) { if (b.length < 4) return; len = b.readUInt16BE(2); off = 4; }
    else if (len === 127) { if (b.length < 10) return; len = Number(b.readBigUInt64BE(2)); off = 10; }
    if (len > MSG_MAX * 2) { close(c); return; }
    const need = off + (masked ? 4 : 0) + len;
    if (b.length < need) return;
    let payload = b.subarray(off + (masked ? 4 : 0), need);
    if (masked) { const m = b.subarray(off, off + 4); payload = Buffer.from(payload); for (let i = 0; i < payload.length; i++) payload[i] ^= m[i & 3]; }
    c.buf = b.subarray(need);
    if (op === 0x8) { close(c); return; }            // close
    if (op === 0x9) { send(c, payload, 0xA); continue; } // ping -> pong
    if (op === 0x1) onMessage(c, payload.toString('utf8'));
  }
}

function frame(data, op = 0x1) {
  const p = Buffer.isBuffer(data) ? data : Buffer.from(data);
  const head = p.length < 126 ? Buffer.from([0x80 | op, p.length]) : p.length < 65536 ? Buffer.from([0x80 | op, 126, p.length >> 8, p.length & 255]) : null;
  if (!head) { const h = Buffer.alloc(10); h[0] = 0x80 | op; h[1] = 127; h.writeBigUInt64BE(BigInt(p.length), 2); return Buffer.concat([h, p]); }
  return Buffer.concat([head, p]);
}
function send(c, data, op = 0x1) { if (!c.socket.destroyed) c.socket.write(frame(data, op)); }
function sendJson(c, obj) { send(c, JSON.stringify(obj)); }
function broadcast(room, obj, except = null) { const f = frame(JSON.stringify(obj)); for (const o of room) if (o !== except && !o.socket.destroyed) o.socket.write(f); }

function onMessage(c, text) {
  // simple flood protection
  const now = Date.now();
  if (now - c.rateT > 1000) { c.rateT = now; c.rate = 0; }
  if (++c.rate > RATE_MAX || text.length > MSG_MAX) return;
  let m;
  try { m = JSON.parse(text); } catch { return; }
  if (!m || typeof m !== 'object') return;
  if (m.t === 'join') {
    const name = String(m.room || 'public').toLowerCase().replace(/[^a-z0-9_.-]/g, '').slice(0, 48) || 'public';
    leave(c);
    let room = rooms.get(name);
    if (!room) { room = new Set(); rooms.set(name, room); }
    if (room.size >= ROOM_MAX) { sendJson(c, { t: 'full', room: name }); return; }
    room.add(c); c.room = name; c.state = {};
    sendJson(c, { t: 'hello', id: c.id, room: name, peers: [...room].filter((o) => o !== c).map((o) => ({ id: o.id, d: o.state })) });
    broadcast(room, { t: 'join', id: c.id }, c);
  } else if (m.t === 's' && c.room && m.d && typeof m.d === 'object') {
    // presence-style shallow merge; null removes a field
    for (const [k, v] of Object.entries(m.d)) { if (v === null) delete c.state[k]; else c.state[k] = v; }
    broadcast(rooms.get(c.room), { t: 's', id: c.id, d: m.d }, c);
  } else if (m.t === 'leave') leave(c);
}

function leave(c) {
  if (!c.room) return;
  const room = rooms.get(c.room);
  if (room) { room.delete(c); broadcast(room, { t: 'left', id: c.id }); if (!room.size) rooms.delete(c.room); }
  c.room = null;
}
function close(c) { try { c.socket.end(frame(Buffer.alloc(0), 0x8)); } catch { /* gone */ } drop(c); }
function drop(c) { if (!clients.has(c)) return; clients.delete(c); leave(c); c.socket.destroy(); }

server.listen(port, () => console.log(`Auto Theft Grand running at http://localhost:${port} (multiplayer relay on ws://localhost:${port}/mp)`));
