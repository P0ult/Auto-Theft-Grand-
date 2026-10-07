// Compare the ported missions' metadata and start points to the browser's actual STORY definitions.
import { STORY } from '../../../src/game/story.js';
import { CityMap } from '../../../src/world/citymap.js';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const map = new CityMap(1337);
const cpp = execFileSync(fileURLToPath(new URL('./simtest.exe', import.meta.url)), ['storymeta'], { encoding: 'utf8' })
  .split(/\r?\n/).filter(s => s.startsWith('story|'));
const ids = new Set(cpp.map(s => s.split('|')[1]));
const js = STORY.missions.filter(m => ids.has(m.id)).map(m => {
  const p = m.start(map.landmarks);
  return `story|${m.id}|${m.title}|${m.contact}|${m.reward || 0}|${(m.requires || []).join(',')}|${p.x.toFixed(2)},${p.z.toFixed(2)}|${m.log || ''}`;
});
if (JSON.stringify(cpp) !== JSON.stringify(js)) {
  console.error('Story metadata differs:', { cpp, js }); process.exitCode = 1;
} else console.log(`Story parity: ${cpp.length} missions match titles, contacts, rewards, prerequisites, starts and logs.`);
