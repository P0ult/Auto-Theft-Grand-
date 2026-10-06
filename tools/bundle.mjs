// Bundles the browser game into one ES module for hosting as a single page (the claude.ai artifact):
//   node tools/bundle.mjs [outDir]      (default: dist/)
// writes outDir/index.html (the page with the stylesheet inlined), outDir/game.js and outDir/assets/audio/.
// Needs esbuild: `npm install` in the repo root.
import { build } from 'esbuild';
import { readFile, writeFile, mkdir, copyFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { join, resolve } from 'node:path';

const root = fileURLToPath(new URL('..', import.meta.url));
const out = resolve(process.argv[2] || join(root, 'dist'));
await mkdir(join(out, 'assets', 'audio'), { recursive: true });
await build({
  entryPoints: [join(root, 'src', 'main.js')], bundle: true, format: 'esm', minify: true, target: 'es2022',
  outfile: join(out, 'game.js'),
  alias: { three: join(root, 'vendor', 'three', 'three.module.min.js'), 'three/addons': join(root, 'vendor', 'three', 'addons') },
  logLevel: 'warning',
});
const css = await readFile(join(root, 'css', 'style.css'), 'utf8');
const html = `<title>Auto Theft Grand</title>
<meta name="description" content="An open-world crime game in the browser: two procedural cities, towns and countryside, drifting, shootouts, police chases, roadblocks and the army at five stars, armoured van heists, a phone with cheats, a weapon wheel, walk-in shops, trains, taxis, jets and helicopters, boats, free roam admin tools, online multiplayer and a 35-mission story.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Anton&family=Bebas+Neue&family=Permanent+Marker&family=Rubik:wght@400;600;700&display=swap">
<style>
:root { color-scheme: dark; padding: 0 !important; }
${css}
</style>
<div id="game"></div>
<script type="module" src="game.js"></script>
`;
await writeFile(join(out, 'index.html'), html);
await copyFile(join(root, 'assets', 'audio', 'wasted.mp3'), join(out, 'assets', 'audio', 'wasted.mp3'));
console.log('bundled to', out);
