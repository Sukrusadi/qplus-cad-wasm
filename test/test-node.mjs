// qpcad wasm'ını Node'da dener: QP_WASM_NODE=1 ./build.sh ile derlenmiş modül gerekir.
// Kullanım: node test/test-node.mjs [dosya.step|.igs|.stl ...]  (dosya yoksa yalnız üretilen STL küp)
import { readFileSync } from 'node:fs';
import { basename, dirname, extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dir = process.env.QPCAD_DIR ?? join(root, 'out-node');
const { default: createQpCad } = await import(join(dir, 'qpcad.mjs'));

// 10 mm küp, 12 üçgen, ASCII STL. Beklenen hacim 1000 mm³.
function cubeStl() {
  const v = [[0,0,0],[10,0,0],[10,10,0],[0,10,0],[0,0,10],[10,0,10],[10,10,10],[0,10,10]];
  const f = [[0,2,1],[0,3,2],[4,5,6],[4,6,7],[0,1,5],[0,5,4],[1,2,6],[1,6,5],[2,3,7],[2,7,6],[3,0,4],[3,4,7]];
  let s = 'solid kup\n';
  for (const t of f) {
    s += ' facet normal 0 0 0\n  outer loop\n';
    for (const i of t) s += `   vertex ${v[i].join(' ')}\n`;
    s += '  endloop\n endfacet\n';
  }
  return new TextEncoder().encode(s + 'endsolid kup\n');
}

async function run(name, bytes, { cancelAt } = {}) {
  const ev = [];
  let fsm = null, metrics = null, error = null, lastProgress = 0;
  const M = await createQpCad({
    wasmBinary: readFileSync(join(dir, 'qpcad.wasm')),
    onProgress: (v) => { lastProgress = v; return !(cancelAt && v >= cancelAt); },
    onMesh: (m) => { ev.push('mesh'); fsm = { ...m, bytes: M.FS.readFile(m.fsmPath) }; },
    onMetrics: (j) => { ev.push('metrics'); metrics = JSON.parse(j); },
    onError: (code, message) => { ev.push('error'); error = { code, message }; },
  });
  const path = '/in/' + name;
  M.FS.writeFile(path, bytes);
  const t0 = performance.now();
  const rc = M.ccall('qp_open', 'number', ['string', 'string', 'number'], [path, extname(name).slice(1), 0]);
  const ms = Math.round(performance.now() - t0);
  M.FS.unlink(path);
  console.log(`\n== ${name} (${bytes.length} bayt): rc=${rc}, ${ms} ms, olaylar: ${ev.join(' → ')}, son ilerleme ${lastProgress.toFixed(2)}`);
  if (error) console.log('   hata:', error);
  if (fsm) {
    const magic = new TextDecoder().decode(fsm.bytes.subarray(0, 4));
    console.log(`   onMesh: ${fsm.fsmPath} parça=${fsm.parts} üçgen=${fsm.triangles} kaba=${fsm.coarse} sade=${fsm.simplified} FSM=${fsm.bytes.length} bayt, baş "${magic}"`);
    if (magic !== 'FSM1') throw new Error('FSM başlığı yanlış');
  }
  if (metrics) {
    const { hacim_mm3, alan_mm2, bbox_min, bbox_max, kati, kapali, delikler } = metrics;
    console.log(`   onMetrics: hacim=${hacim_mm3} mm³ alan=${alan_mm2} mm² katı=${kati} kapalı=${kapali} delik=${delikler.length}`);
    console.log(`   bbox ${JSON.stringify(bbox_min)} → ${JSON.stringify(bbox_max)}`);
    if (!('hacim_mm3' in metrics) || !Array.isArray(bbox_min)) throw new Error('metrics eksik');
  }
  return { rc, fsm, metrics, error };
}

let failed = 0;
const cube = await run('kup.stl', cubeStl());
if (cube.rc !== 0 || Math.abs(cube.metrics.hacim_mm3 - 1000) > 0.01) failed++;
const bad = await run('bozuk.step', new TextEncoder().encode('ISO-10303-21;\nbu bir STEP degil\n'));
if (bad.error?.code !== 'okunamadi' && bad.error?.code !== 'geometri-yok') failed++;
const other = await run('parca.dwg', new Uint8Array(16));
if (other.error?.code !== 'bicim') failed++;
const screw = process.argv[2];
if (screw) {
  const c = await run('iptal-' + basename(screw), readFileSync(screw), { cancelAt: 0.5 });
  if (c.rc !== 2 || c.error?.code !== 'cancelled') failed++;
}
for (const f of process.argv.slice(2)) {
  const r = await run(basename(f), readFileSync(f));
  if (r.rc !== 0 || !r.fsm || !r.metrics) failed++;
}
console.log(failed ? `\n${failed} deneme başarısız` : '\nhepsi tamam');
process.exit(failed ? 1 : 0);
