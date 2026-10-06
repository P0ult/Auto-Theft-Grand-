// Headless check of the browser game: loads it in Chromium, waits for it to start, then runs a test script
// that drives the game frame by frame through the page's test hooks (see CLAUDE.md).
//
//   node tools/browser-test/run.mjs "<url>" <out-prefix> <test.mjs>
//   node tools/browser-test/run.mjs "http://localhost:8080/index.html?manual&autostart=free&q=low" out/phone tools/browser-test/tests/phone.mjs
//
// A test exports `async function (page, shot, ev)`: `ev(fn, arg)` runs fn in the page and returns its result,
// `shot(name)` saves <out-prefix>_<name>.png. W / H set the viewport (default 800 x 450).
// Needs Playwright: `npm install` in the repo root, then `npx playwright install chromium`.
import { chromium } from 'playwright';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const [,, url, prefix, scriptPath] = process.argv;
if (!url || !prefix || !scriptPath) { console.log('usage: node run.mjs "<url>" <out-prefix> <test.mjs>'); process.exit(2); }
// software WebGL on Linux machines without a GPU; a real GPU elsewhere
const args = process.platform === 'linux' ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] : ['--ignore-gpu-blocklist'];
const b = await chromium.launch({ args });
const p = await b.newPage({ viewport: { width: +(process.env.W || 800), height: +(process.env.H || 450) } });
const logs = [];
p.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') logs.push(`[${m.type()}] ${m.text()}`); });
p.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}\n${e.stack}`));
await p.goto(url);
try { await p.waitForFunction(() => window.__ready === true, null, { timeout: 120000 }); } catch { console.log('NOT READY'); console.log(logs.join('\n')); await b.close(); process.exit(1); }
const shot = async (name) => { await p.screenshot({ path: `${prefix}_${name}.png`, timeout: 180000 }); };
const ev = async (fn, arg) => { try { return await p.evaluate(fn, arg); } catch (e) { return 'EVAL ERROR ' + e.message; } };
const mod = await import(pathToFileURL(resolve(scriptPath)).href);
try { await mod.default(p, shot, ev); } catch (e) { console.log('SCRIPT ERROR', e.message); }
console.log(logs.filter((l) => !l.includes('ERR_CERT') && !l.includes('KHR_parallel') && !l.includes('404 (File not found)')).slice(0, 40).join('\n'));
await b.close();
