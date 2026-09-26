// The pause menu's Online tab: your name and colour, joining the public world or a private room code,
// who's here (with teleport / waypoint shortcuts) and chat.
import { PLAYER_COLORS } from './net.js';

const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const hex = (c) => '#' + c.toString(16).padStart(6, '0');
const el = (tag, cls, parent, html) => { const e = document.createElement(tag); if (cls) e.className = cls; if (html != null) e.innerHTML = html; if (parent) parent.appendChild(e); return e; };

function randomCode() {
  const A = 'abcdefghjkmnpqrstuvwxyz23456789';
  let s = '';
  for (let i = 0; i < 5; i++) s += A[(Math.random() * A.length) | 0];
  return s;
}

export function renderOnlineTab(game, body) {
  const net = game.net;
  const box = el('div', 'online', body);
  const status = el('div', 'on-status', box);
  const grid = el('div', 'on-grid', box);

  // ---- you
  const me = el('section', 'on-card', grid);
  el('h3', '', me, 'You');
  const nameRow = el('label', 'on-field', me, '<span>Name</span>');
  const name = el('input', '', nameRow); name.maxLength = 16; name.value = net.name; name.spellcheck = false;
  name.onchange = name.onblur = () => { const v = name.value.replace(/[^\p{L}\p{N} _.\-]/gu, '').trim().slice(0, 16); if (v) { net.name = v; net.saveSettings(); } name.value = net.name; };
  const sw = el('div', 'on-swatches', me);
  const paint = () => sw.querySelectorAll('button').forEach((b, i) => b.classList.toggle('on', i === net.color));
  PLAYER_COLORS.forEach((c, i) => { const b = el('button', '', sw); b.style.background = hex(c); b.title = 'Your colour'; b.onclick = () => { net.color = i; net.saveSettings(); paint(); }; });
  paint();
  const pvpRow = el('label', 'on-check', me);
  const pvp = el('input', '', pvpRow); pvp.type = 'checkbox'; pvp.checked = net.pvp;
  el('span', '', pvpRow, 'Player damage — others can hurt you (and you them)');
  pvp.onchange = () => { net.pvp = pvp.checked; net.saveSettings(); };

  // ---- join
  const join = el('section', 'on-card', grid);
  el('h3', '', join, 'Join a world');
  const pub = el('button', 'btn primary', join, 'Join the public world');
  const row = el('div', 'on-row', join);
  const code = el('input', 'on-code', row); code.maxLength = 12; code.placeholder = 'ROOM CODE'; code.value = net.code ? net.code.toUpperCase() : ''; code.spellcheck = false;
  const joinCode = el('button', 'btn', row, 'Join room');
  const newCode = el('button', 'btn', row, 'New room');
  const leave = el('button', 'btn', join, 'Leave multiplayer');
  const help = el('p', 'muted on-help', join);
  const go = async (c) => { name.onchange(); await net.connect(c); refresh(); };
  pub.onclick = () => go('');
  joinCode.onclick = () => { const c = code.value.trim(); if (c) go(c); else code.focus(); };
  code.onkeydown = (e) => { if (e.key === 'Enter') joinCode.onclick(); };
  newCode.onclick = () => { const c = randomCode(); code.value = c.toUpperCase(); go(c); };
  leave.onclick = async () => { await net.disconnect(); refresh(); };

  // ---- players
  const people = el('section', 'on-card on-people', grid);
  const peopleH = el('h3', '', people, 'Players');
  const list = el('div', 'on-list', people);

  // ---- chat
  const chat = el('section', 'on-card on-chat', grid);
  el('h3', '', chat, 'Chat');
  const log = el('div', 'on-log', chat);
  const crow = el('div', 'on-row', chat);
  const say = el('input', '', crow); say.maxLength = 140; say.placeholder = 'Say something… (in game: press / )';
  const send = el('button', 'btn', crow, 'Send');
  send.onclick = () => { if (say.value.trim()) { net.say(say.value); say.value = ''; renderChat(); } };
  say.onkeydown = (e) => { if (e.key === 'Enter') send.onclick(); };

  function renderChat() {
    log.innerHTML = net.chat.slice(-40).map((m) => `<div class="${m.sys ? 'sys' : ''}">${m.sys ? m.text : `<b style="color:${hex(m.color)}">${esc(m.name)}</b> ${m.text}`}</div>`).join('') || '<div class="sys">No messages yet.</div>';
    log.scrollTop = log.scrollHeight;
  }

  function refresh() {
    if (!box.isConnected) return;
    const o = net.options;
    const on = net.online;
    const st = net.status === 'connecting' ? 'Connecting…' : on ? `Online — ${net.code ? `room <b>${esc(net.code.toUpperCase())}</b>` : 'the <b>public world</b>'}${net.linkUp ? '' : ' (reconnecting…)'}` : net.status === 'error' ? 'Couldn\'t connect' : 'Offline';
    status.innerHTML = `<i class="dot ${on && net.linkUp ? 'on' : net.status === 'connecting' ? 'wait' : ''}"></i>${st}<span class="via">via ${esc(net.transportLabel)}</span>`;
    if (net.status === 'error' && net.error) status.innerHTML += `<div class="err">${esc(net.error)}</div>`;
    const avail = !o || !!(o.room || o.ws);
    pub.disabled = joinCode.disabled = newCode.disabled = !avail || net.status === 'connecting';
    leave.style.display = on ? '' : 'none';
    help.innerHTML = !o ? 'Checking how to connect…'
      : o.room ? 'Everyone with this game open can meet in the <b>public world</b>. For a private session, press <b>New room</b> and share the code — friends type it in and press <b>Join room</b>.'
      : o.ws ? `You're connected to the local game server. Friends on your network can open <b>${esc(location.host)}</b> in their browser and join the same room code.`
      : 'Multiplayer isn\'t available on this page. It works in the published claude.ai version of the game, or run the game yourself with <code>node server.mjs</code> and open it in two browsers.';
    const r = net.roster();
    peopleH.textContent = on ? `Players (${r.length + 1})` : 'Players';
    const free = !!game.freeroam?.active;
    list.innerHTML = '';
    if (on) {
      const mine = el('div', 'on-p me', list);
      mine.innerHTML = `<i style="background:${hex(PLAYER_COLORS[net.color])}"></i><b>${esc(net.name)}</b> <span class="muted">(you)</span>`;
    }
    for (const p of r) {
      const line = el('div', 'on-p', list);
      const tags = [p.vehicle ? esc(p.vehicle) : '', p.dead ? 'wasted' : '', p.paused ? 'paused' : '', p.pvp ? '' : 'passive', p.story ? 'story mode' : '', p.guest ? 'guest' : ''].filter(Boolean).join(' · ');
      line.innerHTML = `<i style="background:${hex(p.color)}"></i><b>${esc(p.name)}</b><span class="where">${esc(p.where)} · ${p.dist < 1000 ? Math.round(p.dist) + ' m' : (p.dist / 1000).toFixed(1) + ' km'}${tags ? ' · ' + tags : ''}</span>`;
      const tp = el('button', 'btn small', line, 'Go to');
      tp.disabled = !free;
      tp.title = free ? 'Teleport next to them' : 'Teleporting is a free roam feature';
      tp.onclick = () => { game.hud.closeOverlay(); game.freeroam.teleport({ name: p.name, x: p.x + 4, z: p.z + 4, foot: true }); };
      const wp = el('button', 'btn small', line, 'Waypoint');
      wp.onclick = () => { game.hud.setWaypoint(p.x, p.z); wp.textContent = 'Set ✓'; };
    }
    if (on && !r.length) el('div', 'muted', list, 'Nobody else here yet — share the room code, or wait for someone to join the public world.');
    if (!on) el('div', 'muted', list, 'Join a world to see who\'s playing.');
    renderChat();
  }

  net.onChange = refresh;
  net.onChat = renderChat;
  net.detect().then(refresh);
  refresh();
  return refresh;
}
