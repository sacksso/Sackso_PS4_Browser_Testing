// Prueba del bloque de stubs del prelude (WebSocket, IndexedDB, Intl...): node tools/tests/test_prelude_stubs.js
// Lee el bloque directamente de app/jsprelude.h para que no se pueda desincronizar.
const fs = require('fs'), path = require('path'), assert = require('assert');
const G = globalThis;
const h = fs.readFileSync(path.join(__dirname, '..', '..', 'app', 'jsprelude.h'), 'utf8');
const a = h.indexOf('// ---------------------------------------------------------------- v0.9.4');
const b = h.indexOf('// ---- fin stubs v0.9.4');
assert(a > 0 && b > a, 'no se encuentra el bloque de stubs en jsprelude.h');
const src = h.slice(a, b);
G.location = { origin: 'https://x.test' };
G.Blob = class Blob { constructor(p) { this.parts = p; } };
const realIntl = G.Intl;
// Node ya trae estas APIs; en QuickJS no existen, asi que se quitan para probar las nuestras
for (const k of ['Intl', 'WebSocket', 'BroadcastChannel', 'File', 'FileReader', 'ReadableStream', 'XMLSerializer', 'visualViewport', 'postMessage', 'indexedDB', 'EventSource']) { try { delete G[k]; } catch (e) {} }
new Function('G', src)(G);
const I = G.Intl; G.Intl = realIntl;
(async () => {
  const log = [];
  const ws = new WebSocket('wss://a'); ws.onerror = () => log.push('wserr'); ws.onclose = e => log.push('wsclose' + e.code);
  assert.throws(() => ws.send('x'));
  const idb = indexedDB.open('db'); idb.onerror = () => log.push('idberr');
  const fr = new FileReader(); fr.onerror = () => log.push('frerr'); fr.readAsText(new Blob([]));
  const es = new EventSource('/s'); es.onerror = () => log.push('eserr');
  const c1 = new BroadcastChannel('c'), c2 = new BroadcastChannel('c'); c2.onmessage = e => log.push('bc:' + e.data); c1.postMessage('hola');
  assert.equal(new File(['x'], 'a.txt').name, 'a.txt');
  assert.equal(new I.NumberFormat('en').format(1234567.891), '1,234,567.891');
  assert.equal(new I.NumberFormat('en', { style: 'currency', currency: 'USD' }).format(1234.5), '$1,234.50');
  assert.equal(new I.NumberFormat('en', { style: 'percent' }).format(0.256), '26%');
  assert.equal(new I.NumberFormat('en', { maximumFractionDigits: 0 }).format(2.6), '3');
  assert.equal(new I.PluralRules('en').select(1), 'one');
  assert.equal(new I.PluralRules('en', { type: 'ordinal' }).select(22), 'two');
  assert.equal(new I.ListFormat('en').format(['a', 'b', 'c']), 'a, b, and c');
  assert.equal(new I.RelativeTimeFormat('en').format(-3, 'days'), '3 days ago');
  assert.equal(new I.DateTimeFormat('en').format(new Date(2020, 0, 5)), '2020-01-05');
  assert.equal(new XMLSerializer().serializeToString({ outerHTML: '<b/>' }), '<b/>');
  assert.equal((await new ReadableStream().getReader().read()).done, true);
  await new Promise(r => setTimeout(r, 30));
  assert.deepEqual(log.sort(), ['bc:hola', 'eserr', 'frerr', 'idberr', 'wsclose1006', 'wserr']);
  console.log('test_prelude_stubs OK');
  process.exit(0);
})().catch(e => { console.error('FALLO', e); process.exit(1); });
