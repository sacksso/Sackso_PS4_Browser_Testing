// Prueba en PC (node) del bloque Canvas 2D del prelude: opcodes iguales a los del enum de canvas2d.h y flujo getContext/dibujo contra un __c simulado.
const fs = require('fs'), path = require('path'), vm = require('vm');
const root = path.join(__dirname, '..', '..');
const pre = fs.readFileSync(path.join(root, 'app/jsprelude.h'), 'utf8');
const hdr = fs.readFileSync(path.join(root, 'app/canvas2d.h'), 'utf8');
// 1) opcodes del prelude == enum C2dOp
const enumBody = hdr.match(/enum C2dOp \{([\s\S]*?)\};/)[1].replace(/\s+/g, ' ').split(',').map(s => s.trim()).filter(Boolean);
let n = 0; const fromEnum = {};
for (const e of enumBody) { const m = e.match(/^C2D_([A-Z_]+)(?:\s*=\s*(\d+))?$/); if (m.length) { n = m[2] ? +m[2] : n + 1; fromEnum[m[1]] = n; } }
const O = eval('(' + pre.match(/const O = (\{[^}]*\});/)[1] + ')');
let bad = 0;
for (const k of Object.keys(fromEnum)) if (O[k] !== fromEnum[k]) { console.log('opcode distinto', k, O[k], fromEnum[k]); bad++; }
for (const k of Object.keys(O)) if (fromEnum[k] === undefined) { console.log('opcode sobrante', k); bad++; }
const strEnum = hdr.match(/enum C2dStr \{([\s\S]*?)\};/)[1].replace(/\s+/g, ' ').split(',').map(s => s.trim().split(/\s*=\s*/)[0]).filter(Boolean);
const S = eval('(' + pre.match(/const S = (\{[^}]*\});/)[1] + ')');
strEnum.forEach((nm, i) => { const k = nm.replace('C2S_', ''); if (S[k] !== i) { console.log('str distinto', k); bad++; } });
// 2) flujo con un __c simulado y un mini DOM
const calls = [];
const C = { create: (w, h) => (calls.push(['create', w, h]), 1), resize: (...a) => calls.push(['resize', ...a]), op: (...a) => (calls.push(['op', ...a]), a[1] === O.GRAD_LINEAR ? 7 : 0), str: (...a) => calls.push(['str', ...a]), image: u => u === 'ok.png' ? { id: 9, w: 16, h: 8 } : null, getPixels: () => new Uint8Array(16).buffer, putPixels: (...a) => calls.push(['put', ...a]) };
const attrs = new Map();
class Element { constructor(t) { this.localName = t; this.a = {}; } getAttribute(k) { return k in this.a ? this.a[k] : null; } setAttribute(k, v) { this.a[k] = String(v); } getBoundingClientRect() { return {}; } dispatchEvent(e) { (this.ev = this.ev || []).push(e.type); return true; } }
for (const k of ['width', 'height', 'src']) Object.defineProperty(Element.prototype, k, { get() { return this.getAttribute(k) || ''; }, set(v) { this.setAttribute(k, v); }, configurable: true });
class Event { constructor(t) { this.type = t; } } class KeyboardEvent extends Event { constructor(t, i) { super(t); Object.assign(this, i); } } class MouseEvent extends Event { constructor(t, i) { super(t); Object.assign(this, i); } }
const timers = [];
const G = { __c: C, Element, Event, KeyboardEvent, MouseEvent, N: {}, navigator: {}, setTimeout: f => timers.push(f), dispatchEvent() {}, document: { body: new Element('body'), createElement: t => new Element(t) } };
G.document.body.dispatchEvent = function (e) { (this.ev = this.ev || []).push(e.type + ':' + e.key); };
const src = pre.match(/\/\/ ---- Canvas 2D \(v0\.9\.5\)[\s\S]*?\n\}\)\(\);\n/)[0];
vm.runInNewContext('(function(G,Element,Event,KeyboardEvent,MouseEvent,document,N,setTimeout){' + src + '})(G,Element,Event,KeyboardEvent,MouseEvent,G.document,{now:()=>1},G.setTimeout)', { G, Element, Event, KeyboardEvent, MouseEvent, document: G.document });
const cv = new Element('canvas'); cv.setAttribute('width', '320'); cv.setAttribute('height', '200');
const ctx = cv.getContext('2d');
const ok = (c, m) => { if (!c) { console.log('FALLA:', m); bad++; } };
ok(ctx && calls[0][0] === 'create' && calls[0][1] === 320 && calls[0][2] === 200, 'getContext crea 320x200');
ok(cv.getContext('2d') === ctx, 'getContext devuelve el mismo contexto');
ok(cv.getContext('webgl') === null, 'webgl devuelve null');
ctx.fillStyle = '#f00'; ok(calls.some(c => c[0] === 'str' && c[2] === 0 && c[3] === '#f00'), 'fillStyle -> str');
ctx.fillRect(1, 2, 3, 4); ok(calls.some(c => c[0] === 'op' && c[2] === O.FILLRECT && c[3] === 1 && c[6] === 4), 'fillRect -> op');
ctx.lineWidth = 5; ctx.lineWidth = -1; ok(ctx.lineWidth === 5, 'lineWidth invalido se ignora');
ctx.save(); ctx.fillStyle = '#0f0'; ctx.restore(); ok(ctx.fillStyle === '#f00', 'restore devuelve fillStyle');
const g = ctx.createLinearGradient(0, 0, 10, 0); g.addColorStop(0, 'red'); ctx.fillStyle = g; ok(calls.some(c => c[0] === 'op' && c[2] === O.FILLGRAD && c[3] === 7), 'gradiente asignado');
ctx.drawImage(cv, 5, 6); ok(calls.some(c => c[0] === 'op' && c[2] === O.DRAWIMAGE && c[3] === 1 && c[6] === 320 && c[7] === 200), 'drawImage 3 args usa el tamano del canvas');
const im = new Element('img'); im.src = 'bad.png'; im.onload = () => {}; // sin __iid todavia
im.src = 'ok.png'; ok(im.naturalWidth === 16 && im.width === 16, 'imagen cargada: naturalWidth');
ctx.drawImage(im, 1, 2, 3, 4); ok(calls.some(c => c[0] === 'op' && c[2] === O.DRAWIMAGE && c[3] === 9 && c[4] === 0 && c[5] === 0 && c[6] === 16), 'drawImage 5 args con imagen');
timers.forEach(f => f()); ok(im.ev && im.ev.includes('load'), 'evento load disparado');
const d = ctx.getImageData(0, 0, 2, 2); ok(d.width === 2 && d.data.length === 16, 'getImageData'); ctx.putImageData(d, 0, 0); ok(calls.some(c => c[0] === 'put'), 'putImageData');
cv.width = 640; ok(calls.some(c => c[0] === 'resize' && c[2] === 640), 'cambiar width redimensiona'); ok(cv.width === 640, 'width numerico');
G.__psbKey(true, 'ArrowUp', 'ArrowUp', 38); ok(G.document.body.ev.includes('keydown:ArrowUp'), 'tecla hacia la pagina');
G.__psbPad([0, 0, 0, 0], [1, 0]); const gp = G.navigator.getGamepads()[0]; ok(gp && gp.buttons[0].pressed && !gp.buttons[1].pressed && gp.mapping === 'standard', 'gamepad');
console.log(bad ? 'test_canvas_prelude: ' + bad + ' FALLOS' : 'test_canvas_prelude OK'); process.exit(bad ? 1 : 0);
