// Renders sounds from src/game/audio.js with an OfflineAudioContext and prints, like Tools/audiotest.cpp, each
// one's peak and its loudness (RMS dBFS) every 50 ms. Run it on any page of the game (it only imports audio.js).
export default async function (p) {
  const lines = await p.evaluate(async () => {
    const { Audio } = await import('/src/game/audio.js');
    const out = [];
    const SR = 48000;
    const report = (name, buf) => {
      const L = buf.getChannelData(0), R = buf.getChannelData(1);
      let peak = 0; for (let i = 0; i < L.length; i++) peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
      const win = SR / 20; const parts = [];
      for (let i = 0; i + win <= L.length; i += win) { let e = 0; for (let k = i; k < i + win; k++) e += L[k] * L[k] + R[k] * R[k]; parts.push(Math.max(-90, 10 * Math.log10(e / (2 * win) + 1e-12)).toFixed(0)); }
      out.push(`${name} peak ${peak.toFixed(2)} | ${parts.join(' ')}`);
    };
    const names = ['pistol', 'smg', 'rifle', 'sniper', 'shotgun', 'rpg', 'explosion', 'crash', 'glass', 'metalhit', 'punch', 'bark', 'moo', 'pickup', 'cash', 'passed', 'failed', 'wasted', 'alarm', 'trainhorn', 'thunder', 'splash'];
    for (const name of names) {
      const sec = name === 'thunder' || name === 'wasted' || name === 'explosion' ? 4 : 1.5;
      const ctx = new OfflineAudioContext(2, Math.round(SR * (sec + 0.5)), SR);
      const Real = window.AudioContext;
      window.AudioContext = function () { return ctx; };
      const game = { settings: {}, camera: { position: { x: 0, y: 0, z: 0 } }, hud: null };
      const a = new Audio(game);
      a.loadSample = () => {};
      a.init();
      window.AudioContext = Real;
      // (fired 0.5 s in, once everything has settled; the report starts there)
      ctx.suspend(0.5).then(() => { a._make(name, a.sfx, 1); ctx.resume(); });
      const buf = await ctx.startRendering();
      const cut = new AudioBuffer({ length: Math.round(SR * sec), numberOfChannels: 2, sampleRate: SR });
      for (let c = 0; c < 2; c++) cut.copyToChannel(buf.getChannelData(c).subarray(Math.round(SR * 0.5)), c);
      report(name, cut);
    }
    return out;
  });
  for (const l of lines) console.log(l);
}
