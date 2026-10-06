// Lets plain node import the browser game's modules: resolves the bare 'three' import to the vendored copy.
//   node --import ./three-hook.mjs dumpworld.mjs
import { register } from 'node:module';
register('data:text/javascript,' + encodeURIComponent(`
const THREE = ${JSON.stringify(new URL('../../../vendor/three/three.module.min.js', import.meta.url).href)};
export async function resolve(spec, ctx, next) {
  if (spec === 'three') return { url: THREE, shortCircuit: true };
  return next(spec, ctx);
}`));
