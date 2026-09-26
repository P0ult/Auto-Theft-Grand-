// Los Soles Cabs.
//  - Whistle for a cab on foot (H): the nearest free cab pulls up at the kerb, or one is sent from a nearby
//    street. Get in the back (F), pick a destination (or use the map waypoint) and ride along with the meter
//    running. Space skips the trip for the estimated fare; F asks the driver to pull over.
//  - Climbing into the back of any cab that has a driver (G) starts a ride the same way.
//  - Taxi driver side job (J while driving a cab): fares wait at the kerb; pick them up, get them where they
//    want to go before the clock runs out, and earn the fare plus a tip for speed. Every fifth fare in a row
//    pays a bonus.
import * as THREE from 'three';
import { LaneDriver, Traffic } from './traffic.js';
import { RouteDriver } from './missions.js';
import { clamp, dist2, pick, rand } from '../core/utils.js';

const FLAG = 4;      // $ when the meter starts
const PER_KM = 9;    // $ per km driven
const JOB_LINES = {
  hello: ['Thank god, a cab!', 'Finally. I\'ve been waving for ages.', 'You free? Great.', 'Hey! Over here!', 'Am I glad to see you.'],
  fast: ['Now THAT is driving!', 'Keep the change, speed racer.', 'Record time! Here, take a tip.'],
  slow: ['I could have walked faster.', 'Forget it, I\'ll walk.', 'Worst. Cab. Ever.'],
  bye: ['Thanks, driver.', 'Appreciate it.', 'Have a good one.', 'Cheers!'],
};

// A driver that just sits still (while the passenger chooses where to go)
class HoldDriver {
  constructor(veh) { this.veh = veh; this.stuck = 0; this.blockedTime = 0; }
  update() { const i = this.veh.input; i.throttle = 0; i.brake = 1; i.steer = 0; i.handbrake = this.veh.speedAbs < 1; }
}

export class TaxiSystem {
  constructor(game) {
    this.game = game;
    this.hail = null;   // cab on its way to pick the player up
    this.ride = null;   // the player riding in the back of a cab
    this.job = null;    // taxi driver side job
    this.panel = null;
    this._jobHintShown = false;
  }

  isCab(v) { return !!v?.def?.taxi; }
  _aiDriven(v) { return !!(v && !v.remote && v.driver && !v.driver.isPlayer && !v.driver.remote && !v.driver.dead && !v.isWrecked && !v.removed); }
  get _ctl() { const g = this.game; return g.input.enabled && !g.cutscene && !g.player.dead && g.gameplay?.state === 'playing' && !g.hud?.menuOpen; }

  // ------------------------------------------------------------------ frame
  update(dt) {
    const g = this.game, p = g.player, input = g.input;
    const ctl = this._ctl;
    if (ctl && !p.vehicle && input.hit('hail')) this.hailCab();
    if (ctl && !p.vehicle && input.hit('passenger')) this.enterAsPassenger();
    if (this.hail) this._updateHail(dt);
    if (this.held) this._updateHeld(dt);

    const v = p.vehicle;
    if (!this.ride && v && p.seat > 0 && this.isCab(v) && this._aiDriven(v) && !g.vehicles.isBusy(p)) this._beginRide(v);
    if (this.ride) this._updateRide(dt, ctl);

    if (v && p.seat === 0 && this.isCab(v)) {
      if (ctl && input.hit('taxiJob')) { if (this.job) this.endJob('Off duty. Thanks for driving with Los Soles Cabs.'); else this.startJob(v); }
      else if (!this.job && !this._jobHintShown && !g.missions?.active) { this._jobHintShown = true; g.hud?.help('You\'re driving a cab. Press <b>J</b> to start picking up fares.', 6); }
    }
    if (this.job) this._updateJob(dt);
    this._updatePanel();
  }

  // Exit requests from the player while riding: ask the driver to pull over first
  handleExit(p) {
    const r = this.ride;
    if (!r || p.vehicle !== r.cab || r.cab.speedAbs < 2) return false;
    if (!r.stopRequest) { r.stopRequest = true; r.cab.ai = new HoldDriver(r.cab); this.game.hud?.help('"Sure thing, pulling over."', 3); }
    return true;
  }

  // Which seat the player should take when entering this vehicle (null = default behaviour)
  seatFor(v, p) {
    if (v.hailedBy !== p || !this._aiDriven(v)) return null;
    return this._rearSeat(v, p.pos);
  }

  _rearSeat(v, pos) {
    const seats = (v.model?.seats?.length || 2) > 2 ? [2, 3] : [1];
    const free = seats.filter((s) => !v.occupants[s]);
    if (!free.length) return [1, 2, 3].find((s) => s < (v.model?.seats?.length || 2) && !v.occupants[s]) ?? null;
    // the door on the player's side
    const [lx] = v.worldToLocal(pos.x, pos.z);
    const want = lx > 0 ? 2 : 3;
    return free.includes(want) ? want : free[0];
  }

  // ------------------------------------------------------------------ hailing
  hailCab() {
    const g = this.game, p = g.player;
    if (this.ride) return;
    p.anim?.play?.('wave');
    g.audio?.play('whistle');
    if (this.hail && !this.hail.cab.removed) { g.hud?.help('Your cab is on its way.', 3); return; }
    let best = null, bd = 120 * 120;
    for (const v of g.vehicles.list) {
      if (!this.isCab(v) || !this._aiDriven(v) || v.hailedBy || v === this.job?.cab) continue;
      if (v.occupants.some((o, i) => i > 0 && o)) continue;
      const d = dist2(v.pos.x, v.pos.z, p.pos.x, p.pos.z);
      if (d < bd) { bd = d; best = v; }
    }
    if (!best) {
      // send one from a street nearby, out of sight
      const smp = Traffic.sampleLane(g, p.pos.x, p.pos.z, 90, 220, (e) => e.type !== 'freeway' && e.type !== 'ramp');
      if (smp) {
        best = g.traffic.spawnCar(smp.start, smp.s0, { type: 'taxi' });
        for (let i = 1; i < best.occupants.length; i++) { const o = best.occupants[i]; if (o) { best.occupants[i] = null; o.vehicle = null; g.peds.remove(o); } }
      }
    }
    if (!best) { g.hud?.help('No cabs are answering out here.', 3); return; }
    this._sendCab(best);
    g.hud?.help('Cab on its way — look for the <b style="color:#ffd23f">yellow</b> blip. Press <b>F</b> by the back door when it pulls up.', 5);
  }

  _sendCab(cab) {
    const g = this.game, p = g.player;
    const spot = g.freeroam.roadSpot(p.pos.x, p.pos.z, false) || { x: p.pos.x, z: p.pos.z };
    cab.persistent = true;
    cab.hailedBy = p;
    cab.ai = new RouteDriver(g, cab, spot, { speed: 15, ignoreLights: false, arriveR: 10 });
    const blip = { x: cab.pos.x, z: cab.pos.z, icon: 'taxi', color: 0xffd23f };
    g.blips.add(blip);
    this.hail = { cab, t: 0, wait: 0, spot, blip, told: false };
  }

  _updateHail(dt) {
    const g = this.game, p = g.player, h = this.hail, cab = h.cab;
    h.t += dt;
    if (!this._aiDriven(cab) || p.dead) return this._cancelHail(cab.isWrecked ? 'Your cab won\'t be coming.' : null);
    if (p.vehicle === cab) { this._clearHail(false); return; }
    if (p.vehicle) return this._cancelHail(null);
    h.blip.x = cab.pos.x; h.blip.z = cab.pos.z;
    // follow the player if they wander off
    if (Math.hypot(h.spot.x - p.pos.x, h.spot.z - p.pos.z) > 35 && !g.vehicles.isBusy(p)) {
      h.spot = g.freeroam.roadSpot(p.pos.x, p.pos.z, false) || { x: p.pos.x, z: p.pos.z };
      cab.ai = new RouteDriver(g, cab, h.spot, { speed: 15, ignoreLights: false, arriveR: 10, snap: false });
    }
    if (cab.ai?.arrived && cab.speedAbs < 1) {
      h.wait += dt;
      if (!h.told) { h.told = true; g.audio?.playAt('horn', cab.pos, 0.6); g.hud?.help('Your cab is here — press <b>F</b> by the back door to get in.', 5); }
      if (h.wait > 60 && !g.vehicles.isBusy(p)) this._cancelHail('The cab driver got tired of waiting.');
    }
    if (h.t > 180) this._cancelHail('Looks like your cab got lost.');
  }

  _clearHail(release) {
    const h = this.hail;
    if (!h) return;
    this.game.blips.delete(h.blip);
    if (release) this._releaseCab(h.cab);
    this.hail = null;
  }
  _cancelHail(msg) { this._clearHail(true); if (msg) this.game.hud?.help(msg, 3); }

  _releaseCab(cab) {
    cab.hailedBy = null;
    cab.persistent = false;
    if (this._aiDriven(cab)) cab.ai = new LaneDriver(this.game, cab);
  }

  // G on foot: climb into the back of the nearest car that has a driver (a cab becomes a ride)
  enterAsPassenger() {
    const g = this.game, p = g.player;
    if (g.vehicles.isBusy(p)) return;
    let v = g.vehicles.nearestEnterable(p.pos, 5.5);
    // another player's vehicle: hop in as their passenger
    let best = 5.5 * 5.5;
    for (const o of g.vehicles.list) {
      if (!o.remote || o.exploded) continue;
      const d = (o.pos.x - p.pos.x) ** 2 + (o.pos.z - p.pos.z) ** 2 - (o.hz * o.hz);
      if (d < best) { best = d; v = o; }
    }
    if (v?.remote) {
      const n = v.model?.seats?.length || 2;
      const seat = [1, 2, 3].find((s) => s < n && !v.occupants[s]);
      if (seat == null) { g.hud?.help('No room in there.', 2); return; }
      g.vehicles.enter(p, v, seat, { force: true });
      return;
    }
    if (!v || v.def.aircraft || v.def.train || v.def.tank) return;
    if (g.missions?.canEnterVehicle && !g.missions.canEnterVehicle(v)) return;
    if (!v.driver || v.driver.isPlayer) { g.tryEnterExit(); return; }
    if (v.speedAbs > 2.5) return;
    const seat = this._rearSeat(v, p.pos);
    if (seat == null) { g.hud?.help('No room in there.', 2); return; }
    // the driver waits while you climb in
    if (this._aiDriven(v) && v.ai) { v.persistent = true; v.ai = new HoldDriver(v); this.held = { v, t: 0 }; }
    g.vehicles.enter(p, v, seat);
  }

  // a car waiting for the player to climb in as a passenger: once aboard (or if they walk off) it carries on
  // (cabs become a ride instead)
  _updateHeld(dt) {
    const g = this.game, p = g.player, h = this.held;
    h.t += dt;
    if (g.vehicles.isBusy(p) && h.t < 6) return;
    if (!(p.vehicle === h.v && this.isCab(h.v))) { h.v.persistent = false; if (this._aiDriven(h.v)) h.v.ai = new LaneDriver(g, h.v); }
    this.held = null;
  }

  // ------------------------------------------------------------------ riding
  _beginRide(cab) {
    const g = this.game;
    this._clearHail(false);
    cab.hailedBy = g.player;
    cab.persistent = true;
    cab.ai = new HoldDriver(cab);
    this.ride = { cab, state: 'choose', dest: null, target: null, dist: 0, fare: FLAG, est: 0, last: cab.pos.clone(), wp: null, t: 0 };
    const wp = g.hud?.waypoint;
    if (wp) this._setDest({ name: 'your waypoint', x: wp.x, z: wp.z }, true);
    else this._openPicker();
  }

  destinations() {
    return this.game.freeroam.destinations().filter((d) => d.group !== 'Quick' || d.name === 'Safehouse').filter((d) => !/restricted|helipad/i.test(d.name));
  }

  estimate(x, z) {
    const c = this.ride?.cab?.pos || this.game.player.pos;
    return Math.round(FLAG + PER_KM * Math.hypot(x - c.x, z - c.z) * 1.3 / 1000);
  }

  _openPicker() {
    const g = this.game, hud = g.hud;
    if (!hud) return;
    g.paused = true;
    g.input.exitLock();
    const o = hud.overlay;
    o.innerHTML = ''; o.className = 'hud-overlay show';
    const panel = document.createElement('div'); panel.className = 'menu-panel taxi-pick'; o.appendChild(panel);
    panel.innerHTML = '<h2>LOS SOLES CABS <small>"Where to?" — or set a waypoint on the map and hop in again.</small></h2>';
    const groups = new Map();
    for (const d of this.destinations()) { if (!groups.has(d.group)) groups.set(d.group, []); groups.get(d.group).push(d); }
    const cols = document.createElement('div'); cols.className = 'tp-cols'; panel.appendChild(cols);
    const money = g.player.money;
    for (const [name, list] of groups) {
      const gr = document.createElement('div'); gr.className = 'tp-group'; cols.appendChild(gr);
      const h3 = document.createElement('h3'); h3.textContent = name; gr.appendChild(h3);
      for (const d of list) {
        const b = document.createElement('button'); b.className = 'tp-btn';
        const fare = this.estimate(d.x, d.z);
        b.innerHTML = `${d.name}<span class="fare${!g.freeroam?.active && fare > money ? ' dear' : ''}">$${fare}</span>`;
        b.onclick = () => { g.audio?.play('ui'); this._picked = true; hud.closeOverlay(); this._setDest(d, false); };
        gr.appendChild(b);
      }
    }
    const foot = document.createElement('div'); foot.className = 'pause-foot'; panel.appendChild(foot);
    const cancel = document.createElement('button'); cancel.className = 'btn'; cancel.textContent = 'Never mind — let me out';
    cancel.onclick = () => { hud.closeOverlay(); };
    foot.appendChild(cancel);
    this._picked = false;
    hud.menuOpen = 'taxi';
  }

  _setDest(d, fromWaypoint) {
    const g = this.game, r = this.ride;
    if (!r) return;
    const spot = g.freeroam.roadSpot(d.x, d.z, false) || { x: d.x, z: d.z, y: g.map.groundHeight(d.x, d.z), yaw: 0 };
    r.dest = d; r.target = spot; r.state = 'ride';
    r.wp = fromWaypoint ? { x: d.x, z: d.z } : null;
    r.est = Math.max(r.fare, this.estimate(spot.x, spot.z));
    r.cab.ai = new RouteDriver(g, r.cab, spot, { speed: 19, ignoreLights: false, arriveR: 13 });
    if (!g.missions?.active) g.hud?.routeTo({ x: spot.x, z: spot.z });
    g.hud?.help(`Heading to <b>${d.name}</b>. Press <b>Space</b> to skip the trip (about $${r.est}), or <b>F</b> to get out.`, 6);
  }

  _updateRide(dt, ctl) {
    const g = this.game, p = g.player, r = this.ride, cab = r.cab;
    r.t += dt;
    if (r.state === 'leaving') {
      if (p.vehicle !== cab && !g.vehicles.isBusy(p)) { this._releaseCab(cab); this._endRide(); }
      return;
    }
    if (p.vehicle !== cab) {
      // bailed out / pulled out / died
      if (r.state === 'ride' && r.dist > 40) this._charge(r.fare, 'Fare so far');
      this._releaseCab(cab); this._endRide();
      return;
    }
    if (!this._aiDriven(cab)) { g.hud?.help('Your driver is out of action. Looks like you\'re walking.', 3); this._endRide(); return; }
    if (r.state === 'choose') {
      if (g.hud?.menuOpen !== 'taxi' && !this._picked) { r.state = 'leaving'; if (!g.vehicles.isBusy(p)) g.vehicles.exit(p); }
      return;
    }
    // meter
    const dd = Math.hypot(cab.pos.x - r.last.x, cab.pos.z - r.last.z);
    r.last.copy(cab.pos);
    if (dd < 60) r.dist += dd;
    r.fare = FLAG + PER_KM * r.dist / 1000;
    // a new waypoint mid-ride: change course
    const wp = g.hud?.waypoint;
    if (r.wp && wp && (wp.x !== r.wp.x || wp.z !== r.wp.z) && !r.skipping) this._setDest({ name: 'your waypoint', x: wp.x, z: wp.z }, true);
    if (ctl && g.input.hit('skipTrip') && !r.skipping && !r.stopRequest) this.skip();
    if (r.stopRequest) {
      if (cab.speedAbs < 1) { this._charge(r.fare, 'Fare'); r.state = 'leaving'; g.vehicles.exit(p); }
      return;
    }
    if (cab.ai?.arrived && cab.speedAbs < 1.2) this._arrive();
  }

  skip() {
    const g = this.game, r = this.ride;
    if (!r || r.state !== 'ride') return;
    r.skipping = true;
    const price = Math.round(Math.max(r.fare, r.est));
    const go = () => {
      if (this.ride !== r || g.player.vehicle !== r.cab) return;
      const cab = r.cab, s = r.target;
      cab.pos.set(s.x, s.y ?? g.map.groundHeight(s.x, s.z), s.z);
      cab.yaw = s.yaw ?? cab.yaw;
      cab.vel.set(0, 0, 0); cab.r = 0;
      r.last.copy(cab.pos);
      r.fare = price; r.dist = (price - FLAG) / PER_KM * 1000;
      cab.ai = new RouteDriver(g, cab, s, { speed: 19, ignoreLights: false, arriveR: 13 });
      // the trip takes time: move the clock on
      g.env.setTime?.(g.env.hours + clamp(r.dist / 15 / 60, 2, 30) / 60);
      g.traffic?.populate(10);
      g.peds?.populate(10);
      r.skipping = false;
    };
    if (!g.hud || this.instant) { go(); return; }
    g.hud.fadeTo(1, 0.35);
    setTimeout(() => { go(); setTimeout(() => g.hud.fadeTo(0, 0.7), 300); }, 420);
  }

  _arrive() {
    const g = this.game, r = this.ride;
    this._charge(r.fare, 'Fare');
    g.hud?.help(`That's $${Math.round(r.fare)}. Thanks for riding with Los Soles Cabs!`, 4);
    r.state = 'leaving';
    g.vehicles.exit(g.player);
  }

  _endRide() {
    const g = this.game;
    if (this.ride && !g.missions?.active && g.hud?.gpsTarget && this.ride.target && g.hud.gpsTarget.x === this.ride.target.x) g.hud.routeTo(null);
    this.ride = null;
  }

  _charge(amount, label) {
    const g = this.game, p = g.player;
    const a = Math.max(1, Math.round(amount));
    const paid = Math.min(p.money, a);
    p.money -= paid;
    g.hud?.moneyFlash(-paid);
    g.audio?.play('cash');
    if (paid < a) g.hud?.help(`${label}: $${a}. You're $${a - paid} short — the driver lets it slide, this time.`, 4);
    g.stats && (g.stats.taxiRides = (g.stats.taxiRides || 0) + 1);
  }

  // ------------------------------------------------------------------ taxi driver job
  startJob(cab) {
    const g = this.game;
    if (g.missions?.active) { g.hud?.help('Finish what you\'re doing first.', 3); return; }
    this.job = { cab, fares: 0, earned: 0, streak: 0, fare: null, next: 1.5 };
    g.hud?.help('On duty! Fares wait at the kerb (<b style="color:#ffd23f">yellow</b> blips). Stop next to them, then get them there before the meter clock runs out. Press <b>J</b> to go off duty.', 8);
    g.audio?.play('checkpoint');
  }

  endJob(msg) {
    const g = this.game, j = this.job;
    if (!j) return;
    const f = j.fare;
    if (f) this._dropFare(f, true);
    g.hud?.setTimer(null);
    g.hud?.setCounter(null);
    if (!g.missions?.active) g.hud?.routeTo(null);
    if (msg) g.hud?.help(`${msg}${j.fares ? ` ${j.fares} fare${j.fares > 1 ? 's' : ''}, $${j.earned} earned.` : ''}`, 5);
    this.job = null;
  }

  _updateJob(dt) {
    const g = this.game, p = g.player, j = this.job, cab = j.cab;
    if (p.vehicle !== cab || p.seat !== 0 || cab.isWrecked || g.missions?.active || p.dead) {
      if (!g.vehicles.isBusy(p) || cab.isWrecked || p.dead) { this.endJob(cab.isWrecked ? 'Your cab is wrecked — shift over.' : 'Off duty.'); }
      return;
    }
    g.hud?.setCounter('FARES', `${j.fares} · $${j.earned}`);
    if (!j.fare) {
      j.next -= dt;
      if (j.next <= 0) { j.fare = this._spawnFare(); if (!j.fare) j.next = 3; }
      return;
    }
    const f = j.fare;
    if (f.ped.dead || f.ped.removed) { g.hud?.help('Your fare is... no longer with us.', 3); this._dropFare(f, false); j.fare = null; j.next = 4; j.streak = 0; return; }
    if (f.stage === 'waiting') {
      const d = Math.hypot(f.ped.pos.x - cab.pos.x, f.ped.pos.z - cab.pos.z);
      f.age += dt;
      if (d < 10 && cab.speedAbs < 1.6 && !g.vehicles.isBusy(f.ped)) {
        const seat = this._rearSeat(cab, f.ped.pos);
        if (seat != null) { g.vehicles.enter(f.ped, cab, seat, { force: true }); f.stage = 'boarding'; f.ped.say?.(pick(JOB_LINES.hello)); }
      } else if (d < 60 && Math.floor(f.age * 0.5) !== Math.floor((f.age - dt) * 0.5)) f.ped.anim?.play?.('wave');
      if (d > 900 || f.age > 240) { this._dropFare(f, true); j.fare = null; j.next = 1; }
    } else if (f.stage === 'boarding') {
      if (f.ped.vehicle === cab && !g.vehicles.isBusy(f.ped)) this._startFareTrip(f);
      else if (!g.vehicles.isBusy(f.ped) && !f.ped.vehicle) f.stage = 'waiting';
    } else if (f.stage === 'riding') {
      f.timer -= dt;
      g.hud?.setTimer(f.timer);
      const d = Math.hypot(f.target.x - cab.pos.x, f.target.z - cab.pos.z);
      if (f.marker) f.marker.group.visible = d < 250;
      if (d < 15 && cab.speedAbs < 1.6) {
        const left = Math.max(0, f.timer);
        const pay = Math.round(10 + f.km * 22 + left * 0.5);
        j.fares++; j.streak++;
        let bonus = 0;
        if (j.streak % 5 === 0) bonus = 50 * (j.streak / 5);
        p.money += pay + bonus; j.earned += pay + bonus;
        g.hud?.moneyFlash(pay + bonus);
        g.audio?.play('cash');
        f.ped.say?.(pick(left > f.limit * 0.35 ? JOB_LINES.fast : JOB_LINES.bye));
        g.hud?.help(bonus ? `<b>${j.streak} in a row!</b> Fare $${pay} + $${bonus} bonus.` : `Fare paid: $${pay}${left > f.limit * 0.35 ? ' (with a tip for the speed)' : ''}.`, 3);
        this._fareOut(f);
      } else if (f.timer <= 0 && !f.late) {
        f.late = true; j.streak = 0;
        f.ped.say?.(pick(JOB_LINES.slow));
        g.hud?.help('Too slow — your fare wants out. Stop the cab.', 4);
      }
      if (f.late && cab.speedAbs < 1.6) this._fareOut(f);
    } else if (f.stage === 'leaving') {
      if (!f.ped.vehicle && !g.vehicles.isBusy(f.ped)) { this._dropFare(f, false); j.fare = null; j.next = rand(2, 5); }
    }
  }

  _spawnFare() {
    const g = this.game, cab = this.job.cab;
    const smp = Traffic.sampleLane(g, cab.pos.x, cab.pos.z, 110, 380, (e) => e.type !== 'freeway' && e.type !== 'ramp' && e.T.cls < 2);
    if (!smp) return null;
    const spot = g.freeroam.roadSpot(smp.x, smp.z, true);
    if (!spot) return null;
    const ped = g.peds.spawnPed(spot.x, spot.z, { persistent: true, y: spot.y });
    ped.setState('guard');
    ped.guardFace = spot.yaw - Math.PI / 2; // face the street
    ped.taxiFare = true;
    const marker = g.pickups.addMarker(spot.x, spot.z, { y: spot.y, color: 0xffd23f, radius: 2.4, icon: 'person', label: 'Fare', arrow: true });
    marker.onEnter = null;
    g.hud?.routeTo({ x: spot.x, z: spot.z });
    return { ped, marker, stage: 'waiting', age: 0, spot };
  }

  _startFareTrip(f) {
    const g = this.game, cab = this.job.cab;
    if (f.marker) { g.pickups.removeMarker(f.marker); f.marker = null; }
    // somewhere 0.4 - 2 km away
    const opts = this.destinations().filter((d) => { const k = Math.hypot(d.x - cab.pos.x, d.z - cab.pos.z); return k > 400 && k < 2000 && !d.platform; });
    let d = opts.length ? pick(opts) : null;
    if (!d) { const s = Traffic.sampleLane(g, cab.pos.x, cab.pos.z, 400, 1200); d = s ? { name: g.map.zoneName?.(s.x, s.z) || 'across town', x: s.x, z: s.z } : { name: 'the safehouse', x: g.map.landmarks.home.x, z: g.map.landmarks.home.z }; }
    const target = g.freeroam.roadSpot(d.x, d.z, false) || d;
    const km = Math.hypot(target.x - cab.pos.x, target.z - cab.pos.z) / 1000;
    f.stage = 'riding'; f.dest = d; f.target = target; f.km = km;
    f.limit = f.timer = Math.round(km * 1000 * 1.35 / 13 + 25);
    f.marker = g.pickups.addMarker(target.x, target.z, { y: target.y, color: 0xffd23f, radius: 4, height: 2.5, icon: 'flag', label: d.name, vehicleOnly: true, arrow: false });
    g.hud?.routeTo({ x: target.x, z: target.z });
    g.hud?.help(`"${pick(['Take me to', 'I need to get to', 'Can you get me to', 'Head for'])} ${d.name}${pick(['.', ', and step on it!', ', please.', ' — I\'m late!'])}"`, 5);
  }

  _fareOut(f) {
    const g = this.game;
    f.stage = 'leaving';
    g.hud?.setTimer(null);
    if (f.marker) { g.pickups.removeMarker(f.marker); f.marker = null; }
    if (f.ped.vehicle) g.vehicles.exit(f.ped);
    if (!g.missions?.active) g.hud?.routeTo(null);
  }

  _dropFare(f, despawn) {
    const g = this.game;
    if (f.marker) { g.pickups.removeMarker(f.marker); f.marker = null; }
    g.hud?.setTimer(null);
    if (!g.missions?.active) g.hud?.routeTo(null);
    const ped = f.ped;
    if (!ped || ped.removed) return;
    if (despawn && !ped.vehicle && !ped.dead) { g.peds.remove(ped); return; }
    if (ped.vehicle && !g.vehicles.isBusy(ped)) g.vehicles.exit(ped);
    ped.persistent = false;
    ped.taxiFare = false;
    if (!ped.dead) ped.setState('wander');
  }

  // ------------------------------------------------------------------ meter panel
  _updatePanel() {
    const r = this.ride;
    const show = r && r.state === 'ride';
    if (!show) { if (this.panel) this.panel.style.display = 'none'; return; }
    if (!this.panel) {
      this.panel = document.createElement('div');
      this.panel.className = 'hud-taxi';
      (this.game.hud?.root || document.body).appendChild(this.panel);
    }
    const cab = r.cab;
    const left = Math.hypot(r.target.x - cab.pos.x, r.target.z - cab.pos.z);
    this.panel.style.display = 'block';
    this.panel.innerHTML = `<div class="t">LOS SOLES CABS</div><div class="m">$${r.fare.toFixed(2)}</div>` +
      `<div class="d">${r.dest.name} · ${(left / 1000).toFixed(1)} km</div>` +
      `<div class="k"><b>SPACE</b> skip trip (~$${Math.round(Math.max(r.fare, r.est))}) · <b>F</b> get out</div>`;
  }
}

export { HoldDriver };
