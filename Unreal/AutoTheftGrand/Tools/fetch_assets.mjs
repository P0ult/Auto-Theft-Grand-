// Downloads the free (CC0) art the Unreal port uses into SourceAssets/, which git ignores. Run it once after a
// clone, then run Tools/import.sh: ATGAssets imports what it finds there into Content/ATG/Imported.
//   node Tools/fetch_assets.mjs            fetch what is missing
//   node Tools/fetch_assets.mjs --force    fetch everything again
// Every asset here is CC0 (public domain); SourceAssets/CREDITS.txt lists where each one came from.
// Archives are unpacked with unzip (.zip) and Windows' own tar (.7z).
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', 'SourceAssets');
const FORCE = process.argv.includes('--force');

// ambientCG materials (2K JPG): colour, normal (GL), roughness, AO and height maps
const TEXTURES = [
	['Asphalt031', 'asphalt: roads, streets, car parks (the game paints its own markings)'],
	['PavingStones150', 'the pavements'],
	['Concrete034', 'concrete: kerbs, plazas, walls'],
	['Bricks105', 'brick buildings'],
	['Grass004', 'lawns and parks'],
	['Ground037', 'bare ground and dirt'],
];

// Poly Haven models (glTF with their textures), at the resolution given
const MODELS = [
	['fire_hydrant', '2k'],
	['street_lamp_01', '2k'],
	['street_lamp_02', '2k'],
	['metal_trash_can', '1k'],
	['concrete_road_barrier', '2k'],
	['modular_street_seating', '1k'],
	['utility_box_01', '2k'],
	['utility_box_02', '2k'],
	['Barrel_01', '2k'],
	['Barrel_02', '2k'],
	['barrel_03', '2k'],
	['wooden_crate_01', '2k'],
	['wooden_crate_02', '2k'],
	['planter_box_01', '2k'],
	['planter_box_02', '2k'],
	['cardboard_box_01', '2k'],
	['trashbag', '2k'],
];

// OpenGameArt files: [folder, file URL, what]
const OGA = [
	['palm_tree_v2', 'https://opengameart.org/sites/default/files/palm_tree_v2.7z', 'Palm tree v2 by Nobiax (Yughues), https://opengameart.org/node/6048 (the author asks for a link to the result: nobiax.deviantart.com)'],
];

// itch.io packs (free, name your own price; fetched the way the page's "No thanks, just take me to the downloads"
// link does)
const ITCH = [
	['quaternius', 'lowpoly-cars', 'Quaternius LowPoly Cars ("Realistic Car Pack - Nov 2018"), FBX/OBJ/Blend'],
];

const credits = [];

async function get(url, opts = {}) {
	const r = await fetch(url, { redirect: 'follow', ...opts });
	if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
	return r;
}

async function download(url, file, opts) {
	const r = await get(url, opts);
	const buf = Buffer.from(await r.arrayBuffer());
	fs.mkdirSync(path.dirname(file), { recursive: true });
	fs.writeFileSync(file, buf);
	return buf.length;
}

function unzip(zip, dir) {
	fs.mkdirSync(dir, { recursive: true });
	if (zip.endsWith('.7z')) execFileSync(path.join(process.env.SystemRoot || 'C:/Windows', 'System32', 'tar.exe'), ['-xf', zip, '-C', dir]);
	else execFileSync('unzip', ['-o', '-q', zip, '-d', dir]);
}

function done(dir) { return !FORCE && fs.existsSync(path.join(dir, '.done')); }
function mark(dir, bytes) { fs.writeFileSync(path.join(dir, '.done'), String(bytes)); }
const mb = (n) => (n / 1e6).toFixed(1) + ' MB';

async function textures() {
	for (const [id, use] of TEXTURES) {
		const dir = path.join(ROOT, 'ambientcg', id);
		credits.push(`${id}: ambientCG, https://ambientcg.com/view?id=${id}, CC0 1.0 (${use})`);
		if (done(dir)) { console.log(`${id}: already here`); continue; }
		const api = await (await get(`https://ambientcg.com/api/v2/full_json?id=${id}&include=downloadData`)).json();
		const dl = api.foundAssets[0].downloadFolders.default.downloadFiletypeCategories.zip.downloads.find((d) => d.attribute === '2K-JPG');
		const zip = path.join(ROOT, 'downloads', `${id}_2K-JPG.zip`);
		const n = await download(dl.fullDownloadPath, zip);
		unzip(zip, dir);
		fs.rmSync(zip);
		mark(dir, n);
		console.log(`${id}: ${mb(n)}`);
	}
}

async function models() {
	for (const [id, res] of MODELS) {
		const dir = path.join(ROOT, 'polyhaven', id);
		credits.push(`${id}: Poly Haven, https://polyhaven.com/a/${id}, CC0 1.0 (${res} glTF)`);
		if (done(dir)) { console.log(`${id}: already here`); continue; }
		const files = await (await get(`https://api.polyhaven.com/files/${id}`)).json();
		const g = files.gltf[res].gltf;
		let n = await download(g.url, path.join(dir, `${id}.gltf`));
		for (const [rel, f] of Object.entries(g.include)) {
			// (the glTF names its buffers and textures by relative paths; nothing may land outside the folder)
			const out = path.resolve(dir, rel);
			if (!out.startsWith(dir + path.sep)) throw new Error(`${id}: bad path ${rel}`);
			n += await download(f.url, out);
		}
		mark(dir, n);
		console.log(`${id}: ${mb(n)}`);
	}
}

async function itch() {
	for (const [user, game, what] of ITCH) {
		const dir = path.join(ROOT, 'itch', game);
		const page = `https://${user}.itch.io/${game}`;
		credits.push(`${game}: ${what}, ${page}, CC0 1.0`);
		if (done(dir)) { console.log(`${game}: already here`); continue; }
		const jar = new Map();
		const keep = (r) => {
			for (const c of r.headers.getSetCookie?.() ?? []) { const [kv] = c.split(';'); const i = kv.indexOf('='); jar.set(kv.slice(0, i), kv.slice(i + 1)); }
			return r;
		};
		const cookie = () => [...jar].map(([k, v]) => `${k}=${v}`).join('; ');
		const html = await keep(await get(page)).text();
		const csrf = /name="csrf_token" value="([^"]+)"/.exec(html)?.[1];
		if (!csrf) throw new Error(`${game}: no csrf token on ${page}`);
		const form = { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded', cookie: cookie() }, body: `csrf_token=${encodeURIComponent(csrf)}` };
		const { url: dlPage } = await keep(await get(`${page}/download_url`, form)).json();
		const list = await keep(await get(dlPage, { headers: { cookie: cookie() } })).text();
		const uploads = [...new Set([...list.matchAll(/data-upload_id="(\d+)"/g)].map((m) => m[1]))];
		if (!uploads.length) throw new Error(`${game}: no files on ${dlPage}`);
		let n = 0;
		for (const up of uploads) {
			const form2 = { ...form, headers: { ...form.headers, cookie: cookie() } };
			const { url, errors } = await keep(await get(`${page}/file/${up}?source=view_game&as_props=1&after_download_lightbox=true`, form2)).json();
			if (!url) throw new Error(`${game}: ${errors}`);
			const zip = path.join(ROOT, 'downloads', `${game}-${up}.zip`);
			const bytes = await download(url, zip);
			unzip(zip, dir);
			fs.rmSync(zip);
			n += bytes;
		}
		mark(dir, n);
		console.log(`${game}: ${mb(n)}`);
	}
}

async function oga() {
	for (const [name, url, what] of OGA) {
		const dir = path.join(ROOT, 'opengameart', name);
		credits.push(`${name}: ${what}, CC0 1.0`);
		if (done(dir)) { console.log(`${name}: already here`); continue; }
		const file = path.join(ROOT, 'downloads', path.basename(new URL(url).pathname));
		const n = await download(url, file);
		unzip(file, dir);
		fs.rmSync(file);
		mark(dir, n);
		console.log(`${name}: ${mb(n)}`);
	}
}

fs.mkdirSync(ROOT, { recursive: true });
await textures();
await models();
await itch();
await oga();
fs.writeFileSync(path.join(ROOT, 'CREDITS.txt'), credits.join('\n') + '\n');
console.log(`done: ${ROOT}`);
