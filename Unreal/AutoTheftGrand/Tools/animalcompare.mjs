// Rig parity against the original animals.js. Compare with simtest.exe animalcmp (the 'rig' lines).
import * as THREE from 'three';
import { Animal, BREEDS } from '../../../src/entities/animals.js';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const g = { scene: new THREE.Scene(), map: { groundHeight: () => 0 } };
const lines = [];
for (const breed of Object.keys(BREEDS)) {
  const a = new Animal(g, breed, 0, 0, { y: 0, yaw: 0 });
  a.id = 7; a.phase = 0.37; a.t = 2.1;
  for (let i = 0; i < 90; i++) {
    a.speed = i < 30 ? a.sp.walk : i < 60 ? (a.sp.bird ? a.sp.fly : a.sp.run) : 0;
    a.state = i >= 60 ? 'sit' : 'wander'; a.flying = !!a.sp.bird && i < 60;
    a.t += 1 / 30; a._animate(1 / 30);
    if (i % 30 === 29) {
      const vals = [a.bodyG.position.y, a.bodyG.rotation.x, a.legs?.[0].rotation.x || 0,
        a.legs?.[2].rotation.x || 0, a.headG?.rotation.x || 0, a.tailG?.rotation.y || 0,
        a.wings?.[0].rotation.z || 0];
      lines.push(`rig ${breed} ${i} ${vals.map(v => v.toFixed(6)).join(' ')}`);
    }
  }
}
if (process.argv.includes('--check')) {
  const cpp = execFileSync(fileURLToPath(new URL('./simtest.exe', import.meta.url)), ['animalcmp'], { encoding: 'utf8' })
    .split(/\r?\n/).filter(s => s.startsWith('rig ')).map(s => s.replaceAll('-0.000000', '0.000000'));
  if (JSON.stringify(cpp) !== JSON.stringify(lines)) {
    console.error('Animal rig differs:', { cpp, js: lines }); process.exitCode = 1;
  } else console.log(`Animal rig parity: ${Object.keys(BREEDS).length} breeds, ${lines.length} poses match to 6 decimal places.`);
} else console.log(lines.join('\n'));
