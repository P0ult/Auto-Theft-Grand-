// Save / load via localStorage (progress, cash, weapons, stats, packages, clock) + settings.
const KEY = 'atg_save_v1';
const SKEY = 'atg_settings_v1';

function read(key) { try { const s = localStorage.getItem(key); return s ? JSON.parse(s) : null; } catch { return null; } }
function write(key, v) { try { localStorage.setItem(key, JSON.stringify(v)); return true; } catch { return false; } }

export class SaveSystem {
  constructor(game) { this.game = game; }

  static loadSettings() { return read(SKEY) || {}; }
  saveSettings() { write(SKEY, this.game.settings); }

  hasSave() { return !!read(KEY); }
  info() { const d = read(KEY); if (!d) return null; return { date: d.date, progress: d.missions?.completed?.length || 0, money: d.money }; }

  save(auto = false) {
    const g = this.game, p = g.player;
    if (g.freeRoam) return false; // free roam (everything unlocked, unlimited cash) never touches the story save
    const weapons = {};
    for (const [id, w] of Object.entries(p.weapons)) weapons[id] = { ammo: Number.isFinite(w.ammo) ? w.ammo : -1, clip: Number.isFinite(w.clip) ? w.clip : -1 };
    const data = {
      version: 1, date: new Date().toISOString(),
      money: p.money, health: p.health, armor: p.armor, weapons, weapon: p.weapon,
      hours: g.env.hours, missions: g.missions.serialize(), stats: g.stats,
      packages: [...g.pickups.collectedPackages], gang: g.missions.gangDensity,
    };
    const ok = write(KEY, data);
    if (!auto && ok) g.audio?.play('checkpoint');
    return ok;
  }

  load() {
    const d = read(KEY);
    if (!d) return false;
    const g = this.game, p = g.player;
    p.money = d.money ?? 0;
    p.health = d.health > 0 ? d.health : p.maxHealth;
    p.armor = d.armor || 0;
    p.weapons = { fist: { ammo: Infinity, clip: Infinity } };
    for (const [id, w] of Object.entries(d.weapons || {})) p.weapons[id] = { ammo: w.ammo < 0 ? Infinity : w.ammo, clip: w.clip < 0 ? Infinity : w.clip };
    p.equip(p.weapons[d.weapon] ? d.weapon : 'fist');
    g.missions.load(d.missions);
    if (d.gang) Object.assign(g.missions.gangDensity, d.gang);
    Object.assign(g.stats, d.stats || {});
    g.pickups.collectedPackages = new Set(d.packages || []);
    g.pickups.refreshPackages();
    g.pickups.spawnSafehouseRewards();
    g.env.setTime(d.hours ?? 9);
    return true;
  }

  clear() { try { localStorage.removeItem(KEY); } catch { /* ignore */ } }
}
