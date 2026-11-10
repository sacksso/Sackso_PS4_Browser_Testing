// Prueba en PC (node) del bloque v0.9.6 del prelude: NodeFilter/TreeWalker/NodeIterator, Path2D, audio silencioso.
// Lee el bloque de app/jsprelude.h y lo ejecuta contra un DOM minimo simulado.  Uso: node tools/tests/test_prelude_v096.js
const fs = require('fs'), path = require('path'), assert = require('assert');
const h = fs.readFileSync(path.join(__dirname, '..', '..', 'app', 'jsprelude.h'), 'utf8');
const a = h.indexOf('// ---------------------------------------------------------------- v0.9.6');
const b = h.indexOf('// ---- fin v0.9.6');
assert(a > 0 && b > a, 'no se encuentra el bloque v0.9.6');
const src = h.slice(a, b);
class Event { constructor(t) { this.type = t; } }
class N { constructor(t, name) { this.nodeType = t; this.name = name; this.childNodes = []; this.parentNode = null; this.listeners = {}; }
  add(c) { c.parentNode = this; this.childNodes.push(c); return c; }
  addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
  dispatchEvent(e) { (this.listeners[e.type] || []).forEach(f => f(e)); return true; }
  setAttribute(k, v) { this[k] = v; } load() { this.loaded = true; } }
class Element extends N { constructor(n) { super(1, n); this.localName = n; } }
class Document extends N {}
const G = { Event, Element, Document, timers: [], document: { createElement: n => new Element(n) }, navigator: {}, screen: { orientation: {} } };
G.setTimeout = (f) => { G.timers.push(f); return G.timers.length; };
new Function('G', 'Element', 'Document', 'document', src)(G, Element, Document, G.document);
// arbol: root > [a > [t1, b], c > [d], #text]
const root = new Element('root'), A = root.add(new Element('a')), t1 = A.add(new N(3, '#t')), B = A.add(new Element('b')),
  C = root.add(new Element('c')), D = C.add(new Element('d')), T = root.add(new N(3, '#t2'));
const names = tw => { const o = []; let n; while ((n = tw.nextNode())) o.push(n.name || n.nodeType); return o; };
assert.deepEqual(names(new G.TreeWalker(root, G.NodeFilter.SHOW_ELEMENT)), ['a', 'b', 'c', 'd']);
assert.deepEqual(names(new G.TreeWalker(root, G.NodeFilter.SHOW_TEXT)), ['#t', '#t2']);
assert.deepEqual(names(new G.TreeWalker(root, G.NodeFilter.SHOW_ELEMENT, n => n.name === 'a' ? 2 : 1)), ['c', 'd']);      // REJECT salta el subarbol
assert.deepEqual(names(new G.TreeWalker(root, G.NodeFilter.SHOW_ELEMENT, { acceptNode: n => n.name === 'b' ? 3 : 1 })), ['a', 'c', 'd']);
const tw = new G.TreeWalker(root, G.NodeFilter.SHOW_ELEMENT); tw.currentNode = D;
assert.equal(tw.previousNode().name, 'c'); assert.equal(tw.previousNode().name, 'b'); assert.equal(tw.parentNode().name, 'a');
const it = new G.NodeIterator(root, G.NodeFilter.SHOW_ELEMENT); const seq = []; let x; while ((x = it.nextNode())) seq.push(x.name);
assert.deepEqual(seq, ['root', 'a', 'b', 'c', 'd']); assert.equal(it.previousNode().name, 'c');
// Path2D
const p = new G.Path2D(); p.moveTo(1, 2); p.lineTo(3, 4); const q = new G.Path2D(p); q.addPath(p); assert.equal(q.__ops.length, 4);
// Audio
const au = new G.Audio('x.ogg'); const ev = []; au.addEventListener('canplaythrough', () => ev.push('cpt')); G.timers.splice(0).forEach(f => f());
assert.deepEqual(ev, ['cpt']); assert.equal(au.canPlayType('audio/mp3'), ''); assert.equal(au.readyState, 4);
const ac = new G.AudioContext(); assert.equal(ac.state, 'running'); ac.createGain().gain.setValueAtTime(1, 0); ac.createOscillator().connect(ac.destination);
ac.decodeAudioData(new ArrayBuffer(8)).then(buf => { assert(buf.duration > 0); assert(buf.getChannelData(0) instanceof Float32Array); console.log('OK test_prelude_v096'); });
assert.equal(G.webkitAudioContext, G.AudioContext); assert.equal(G.document !== null, true);
assert.equal(typeof Element.prototype.requestFullscreen, 'function'); assert.equal(G.navigator.vibrate(), false);
