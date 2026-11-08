#pragma once
// Prelude de JavaScript (v0.9.0): construye document/window/Element... sobre las primitivas nativas __d (DOM) y __n (sistema).
static const char* const kJsPrelude = R"JS(
(function (G) {
'use strict';
const D = __d, N = __n;
const cache = new Map();
const kebab = s => s.replace(/[A-Z]/g, m => '-' + m.toLowerCase());
const camel = s => s.replace(/-([a-z])/g, (m, c) => c.toUpperCase());
const idOf = n => (n && n.__id !== undefined) ? n.__id : -1;

// ---------------------------------------------------------------- eventos
class Event {
  constructor(type, init) {
    init = init || {};
    this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable;
    this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.timeStamp = N.now();
    this.__stop = false; this.__stopImm = false; this.isTrusted = false;
  }
  preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
  stopPropagation() { this.__stop = true; }
  stopImmediatePropagation() { this.__stop = true; this.__stopImm = true; }
  get cancelBubble() { return this.__stop; }
  composedPath() { return []; }
}
class CustomEvent extends Event { constructor(t, i) { super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; } }
class UIEvent extends Event {}
class MouseEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.clientX = i.clientX || 0; this.clientY = i.clientY || 0; this.button = i.button || 0; } }
class KeyboardEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.key = i.key || ''; this.code = i.code || ''; this.keyCode = i.keyCode || 0; } }
class EventTarget {
  addEventListener(t, fn, opt) {
    if (!fn) return;
    const L = this.__l || (Object.defineProperty(this, '__l', { value: {}, enumerable: false }), this.__l);
    const once = !!(opt && typeof opt === 'object' && opt.once);
    const a = L[t] || (L[t] = []);
    if (!a.some(x => x.fn === fn)) a.push({ fn, once });
  }
  removeEventListener(t, fn) { const a = this.__l && this.__l[t]; if (a) { const i = a.findIndex(x => x.fn === fn); if (i >= 0) a.splice(i, 1); } }
  dispatchEvent(ev) {
    ev.target = this;
    const path = [];
    for (let n = this; n; n = n.__parentTarget()) path.push(n);
    for (const t of path) {
      if (ev.type !== 'DOMContentLoaded' && t !== this && !ev.bubbles) break;
      ev.currentTarget = t;
      const h = t['on' + ev.type];
      if (typeof h === 'function') { try { h.call(t, ev); } catch (e) { N.error(e); } }
      const a = t.__l && t.__l[ev.type];
      if (a) for (const x of a.slice()) {
        if (x.once) t.removeEventListener(ev.type, x.fn);
        try { typeof x.fn === 'function' ? x.fn.call(t, ev) : x.fn.handleEvent(ev); } catch (e) { N.error(e); }
        if (ev.__stopImm) break;
      }
      if (ev.__stop) break;
    }
    return !ev.defaultPrevented;
  }
  __parentTarget() { return null; }
}

// ---------------------------------------------------------------- estilo (style="..." <-> objeto)
function parseStyle(s) {
  const m = new Map();
  (s || '').split(';').forEach(d => { const i = d.indexOf(':'); if (i > 0) { const k = d.slice(0, i).trim().toLowerCase(), v = d.slice(i + 1).trim(); if (k) m.set(k, v); } });
  return m;
}
function makeStyle(el) {
  const get = () => parseStyle(el.getAttribute('style'));
  const put = m => { const s = [...m].map(([k, v]) => k + ':' + v).join(';'); if (s) el.setAttribute('style', s); else el.removeAttribute('style'); };
  const api = {
    getPropertyValue: k => get().get(String(k).toLowerCase()) || '',
    setProperty: (k, v) => { const m = get(); if (v === '' || v == null) m.delete(String(k).toLowerCase()); else m.set(String(k).toLowerCase(), String(v)); put(m); },
    removeProperty: k => { const m = get(); const o = m.get(String(k).toLowerCase()) || ''; m.delete(String(k).toLowerCase()); put(m); return o; },
    item: i => [...get().keys()][i] || '',
  };
  return new Proxy(api, {
    get(t, p) {
      if (p in t) return t[p];
      if (p === 'cssText') return el.getAttribute('style') || '';
      if (p === 'length') return get().size;
      if (typeof p !== 'string') return undefined;
      return get().get(kebab(p)) || '';
    },
    set(t, p, v) {
      if (p === 'cssText') { v ? el.setAttribute('style', String(v)) : el.removeAttribute('style'); return true; }
      if (typeof p !== 'string') return true;
      api.setProperty(kebab(p), v); return true;
    },
  });
}

// ---------------------------------------------------------------- nodos
class Node extends EventTarget {
  constructor(id) { super(); Object.defineProperty(this, '__id', { value: id }); }
  __parentTarget() { const p = this.parentNode; return p || (this === G.document ? G : null); }
  get nodeType() { return D.type(this.__id); }
  get nodeName() { return this.nodeType === 3 ? '#text' : this.nodeType === 8 ? '#comment' : this.nodeType === 11 ? '#document-fragment' : this.nodeType === 9 ? '#document' : D.tag(this.__id).toUpperCase(); }
  get parentNode() { return wrap(D.parent(this.__id)); }
  get parentElement() { const p = this.parentNode; return p && p.nodeType === 1 ? p : null; }
  get firstChild() { return wrap(D.first(this.__id)); }
  get lastChild() { return wrap(D.last(this.__id)); }
  get nextSibling() { return wrap(D.next(this.__id)); }
  get previousSibling() { return wrap(D.prev(this.__id)); }
  get childNodes() { const a = []; for (let c = D.first(this.__id); c >= 0; c = D.next(c)) a.push(wrap(c)); return a; }
  get children() { return this.childNodes.filter(n => n.nodeType === 1); }
  get childElementCount() { return this.children.length; }
  get firstElementChild() { return this.children[0] || null; }
  get lastElementChild() { const c = this.children; return c[c.length - 1] || null; }
  get nextElementSibling() { let n = this.nextSibling; while (n && n.nodeType !== 1) n = n.nextSibling; return n; }
  get previousElementSibling() { let n = this.previousSibling; while (n && n.nodeType !== 1) n = n.previousSibling; return n; }
  get ownerDocument() { return G.document; }
  get isConnected() { for (let n = this; n; n = n.parentNode) if (n === G.document) return true; return false; }
  get textContent() { return this.nodeType === 3 || this.nodeType === 8 ? D.data(this.__id) : D.textContent(this.__id); }
  set textContent(v) { v = v == null ? '' : String(v); if (this.nodeType === 3 || this.nodeType === 8) D.setData(this.__id, v); else D.setText(this.__id, v); }
  get nodeValue() { return this.nodeType === 3 || this.nodeType === 8 ? D.data(this.__id) : null; }
  set nodeValue(v) { if (this.nodeType === 3 || this.nodeType === 8) D.setData(this.__id, String(v)); }
  hasChildNodes() { return D.first(this.__id) >= 0; }
  contains(o) { for (let n = o; n; n = n.parentNode) if (n === this) return true; return false; }
  getRootNode() { let n = this; while (n.parentNode) n = n.parentNode; return n; }
  appendChild(c) { D.append(this.__id, idOf(c)); return c; }
  insertBefore(c, ref) { D.insertBefore(this.__id, idOf(c), ref ? idOf(ref) : -1); return c; }
  removeChild(c) { D.remove(idOf(c)); return c; }
  replaceChild(n, o) { D.insertBefore(this.__id, idOf(n), idOf(o)); D.remove(idOf(o)); return o; }
  cloneNode(deep) { return wrap(D.clone(this.__id, !!deep)); }
  normalize() {}
  append(...a) { for (const n of a) this.appendChild(typeof n === 'object' ? n : G.document.createTextNode(String(n))); }
  prepend(...a) { const f = this.firstChild; for (const n of a) this.insertBefore(typeof n === 'object' ? n : G.document.createTextNode(String(n)), f); }
  before(...a) { const p = this.parentNode; if (p) for (const n of a) p.insertBefore(typeof n === 'object' ? n : G.document.createTextNode(String(n)), this); }
  after(...a) { const p = this.parentNode, nx = this.nextSibling; if (p) for (const n of a) p.insertBefore(typeof n === 'object' ? n : G.document.createTextNode(String(n)), nx); }
  remove() { D.remove(this.__id); }
  replaceWith(...a) { this.before(...a); this.remove(); }
  replaceChildren(...a) { D.setText(this.__id, ''); this.append(...a); }
  isEqualNode(o) { return this === o; }
  isSameNode(o) { return this === o; }
}
Node.ELEMENT_NODE = 1; Node.TEXT_NODE = 3; Node.COMMENT_NODE = 8; Node.DOCUMENT_NODE = 9; Node.DOCUMENT_FRAGMENT_NODE = 11;
class Text extends Node { get data() { return D.data(this.__id); } set data(v) { D.setData(this.__id, String(v)); } get length() { return this.data.length; } get wholeText() { return this.data; } }
class Comment extends Node { get data() { return D.data(this.__id); } set data(v) { D.setData(this.__id, String(v)); } }
class DocumentFragment extends Node {
  querySelector(s) { return qsOne(this.__id, s); } querySelectorAll(s) { return qsAll(this.__id, s); }
  getElementById(i) { return qsOne(this.__id, '#' + i); }
}
function wrap(id) {
  if (id < 0) return null;
  let w = cache.get(id);
  if (w) return w;
  const t = D.type(id);
  w = t === 1 ? new Element(id) : t === 3 ? new Text(id) : t === 8 ? new Comment(id) : t === 11 ? new DocumentFragment(id) : t === 9 ? G.document : new Node(id);
  cache.set(id, w);
  return w;
}
function qsOne(root, sel) { const r = D.qsa(root, String(sel), true); if (r === null) throw new DOMException('"' + sel + '" is not a valid selector', 'SyntaxError'); return r.length ? wrap(r[0]) : null; }
function qsAll(root, sel) { const r = D.qsa(root, String(sel), false); if (r === null) throw new DOMException('"' + sel + '" is not a valid selector', 'SyntaxError'); return r.map(wrap); }
class DOMException extends Error { constructor(m, n) { super(m); this.name = n || 'Error'; } }

const BOOL = ['checked', 'disabled', 'hidden', 'selected', 'readOnly', 'required', 'multiple', 'autofocus', 'async', 'defer', 'open', 'controls', 'loop', 'muted', 'autoplay'];
const STR = ['href', 'src', 'type', 'name', 'placeholder', 'title', 'alt', 'rel', 'target', 'action', 'method', 'for', 'lang', 'dir', 'role', 'tabIndex', 'width', 'height', 'min', 'max', 'step', 'pattern', 'autocomplete', 'media', 'content', 'charset', 'download', 'srcset', 'sizes', 'poster', 'preload'];
class Element extends Node {
  get tagName() { const t = D.tag(this.__id).toUpperCase(); return t; }
  get localName() { return D.tag(this.__id); }
  get id() { return this.getAttribute('id') || ''; } set id(v) { this.setAttribute('id', v); }
  get className() { return this.getAttribute('class') || ''; } set className(v) { this.setAttribute('class', v); }
  get classList() {
    const el = this, tok = () => (el.getAttribute('class') || '').split(/\s+/).filter(Boolean), put = a => el.setAttribute('class', a.join(' '));
    return {
      add: (...c) => { const a = tok(); c.forEach(x => { if (a.indexOf(x) < 0) a.push(x); }); put(a); },
      remove: (...c) => { put(tok().filter(x => c.indexOf(x) < 0)); },
      toggle: (c, f) => { const a = tok(), h = a.indexOf(c) >= 0, w = f === undefined ? !h : !!f; if (w && !h) { a.push(c); put(a); } else if (!w && h) put(a.filter(x => x !== c)); return w; },
      contains: c => tok().indexOf(c) >= 0, item: i => tok()[i] || null, replace: (o, n) => { const a = tok(); const i = a.indexOf(o); if (i < 0) return false; a[i] = n; put(a); return true; },
      get length() { return tok().length; }, get value() { return el.className; }, toString: () => el.className, forEach: f => tok().forEach(f), [Symbol.iterator]: () => tok()[Symbol.iterator](),
    };
  }
  get attributes() { return D.attrNames(this.__id).map(n => ({ name: n, value: D.getAttr(this.__id, n), nodeName: n, nodeValue: D.getAttr(this.__id, n) })); }
  getAttribute(n) { const v = D.getAttr(this.__id, String(n).toLowerCase()); return v === undefined ? null : v; }
  setAttribute(n, v) { D.setAttr(this.__id, String(n), String(v)); }
  removeAttribute(n) { D.rmAttr(this.__id, String(n)); }
  hasAttribute(n) { return D.getAttr(this.__id, String(n).toLowerCase()) !== undefined; }
  toggleAttribute(n, f) { const h = this.hasAttribute(n), w = f === undefined ? !h : !!f; if (w && !h) this.setAttribute(n, ''); else if (!w && h) this.removeAttribute(n); return w; }
  getAttributeNames() { return D.attrNames(this.__id); }
  get innerHTML() { return D.inner(this.__id); } set innerHTML(v) { D.setInner(this.__id, v == null ? '' : String(v)); }
  get outerHTML() { return D.outer(this.__id); }
  set outerHTML(v) { const p = this.parentNode; if (!p) return; const t = G.document.createElement('div'); t.innerHTML = String(v); const nx = this.nextSibling; this.remove(); while (t.firstChild) p.insertBefore(t.firstChild, nx); }
  get innerText() { return this.textContent; } set innerText(v) { this.textContent = v; }
  insertAdjacentHTML(pos, html) {
    const t = G.document.createElement('div'); t.innerHTML = String(html);
    const kids = t.childNodes; pos = String(pos).toLowerCase();
    if (pos === 'beforeend') kids.forEach(k => this.appendChild(k));
    else if (pos === 'afterbegin') { const f = this.firstChild; kids.forEach(k => this.insertBefore(k, f)); }
    else if (pos === 'beforebegin') kids.forEach(k => this.before(k));
    else if (pos === 'afterend') { let r = this; kids.forEach(k => { r.after(k); r = k; }); }
  }
  insertAdjacentElement(pos, el) { const p = String(pos).toLowerCase(); if (p === 'beforeend') this.appendChild(el); else if (p === 'afterbegin') this.prepend(el); else if (p === 'beforebegin') this.before(el); else this.after(el); return el; }
  insertAdjacentText(pos, t) { this.insertAdjacentElement(pos, G.document.createTextNode(t)); }
  get style() { return this.__style || (Object.defineProperty(this, '__style', { value: makeStyle(this), enumerable: false }), this.__style); }
  set style(v) { this.style.cssText = v; }
  get dataset() {
    const el = this;
    return new Proxy({}, {
      get: (t, p) => { if (typeof p !== 'string') return undefined; const v = el.getAttribute('data-' + kebab(p)); return v === null ? undefined : v; },
      set: (t, p, v) => { el.setAttribute('data-' + kebab(String(p)), v); return true; },
      deleteProperty: (t, p) => { el.removeAttribute('data-' + kebab(String(p))); return true; },
      has: (t, p) => el.hasAttribute('data-' + kebab(String(p))),
      ownKeys: () => el.getAttributeNames().filter(n => n.startsWith('data-')).map(n => camel(n.slice(5))),
      getOwnPropertyDescriptor: (t, p) => el.hasAttribute('data-' + kebab(String(p))) ? { value: el.getAttribute('data-' + kebab(String(p))), enumerable: true, configurable: true, writable: true } : undefined,
    });
  }
  get value() { const t = this.localName; if (t === 'textarea') return this.textContent; if (t === 'select') { const o = this.querySelector('option[selected]') || this.querySelector('option'); return o ? (o.getAttribute('value') !== null ? o.getAttribute('value') : o.textContent) : ''; } const v = this.getAttribute('value'); return v === null ? '' : v; }
  set value(v) { if (this.localName === 'textarea') this.textContent = v; else this.setAttribute('value', v); }
  get checked() { return this.hasAttribute('checked'); } set checked(v) { v ? this.setAttribute('checked', '') : this.removeAttribute('checked'); }
  get selectedIndex() { return 0; }
  get options() { return this.querySelectorAll('option'); }
  get form() { return this.closest('form'); }
  get text() { return this.textContent; } set text(v) { this.textContent = v; }
  matches(s) { return D.matches(this.__id, String(s)); }
  closest(s) { return wrap(D.closest(this.__id, String(s))); }
  querySelector(s) { return qsOne(this.__id, s); }
  querySelectorAll(s) { return qsAll(this.__id, s); }
  getElementsByTagName(t) { return qsAll(this.__id, String(t)); }
  getElementsByClassName(c) { return qsAll(this.__id, String(c).trim().split(/\s+/).map(x => '.' + x).join('')); }
  getBoundingClientRect() { return { x: 0, y: 0, top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0 }; }
  getClientRects() { return []; }
  get offsetWidth() { return 0; } get offsetHeight() { return 0; } get offsetTop() { return 0; } get offsetLeft() { return 0; }
  get clientWidth() { return 0; } get clientHeight() { return 0; } get scrollWidth() { return 0; } get scrollHeight() { return 0; }
  get scrollTop() { return 0; } set scrollTop(v) {} get scrollLeft() { return 0; } set scrollLeft(v) {}
  get offsetParent() { return null; }
  focus() {} blur() {} scrollIntoView() {} scrollTo() {} select() {} setSelectionRange() {}
  click() { const e = new MouseEvent('click', { bubbles: true, cancelable: true }); this.dispatchEvent(e); }
  animate() { return { cancel() {}, finish() {}, onfinish: null, play() {}, pause() {} }; }
  attachShadow() { return this; }
  getContext() { return null; }
  toDataURL() { return 'data:,'; }
  play() { return Promise.resolve(); } pause() {} load() {}
  get paused() { return true; } get currentTime() { return 0; } set currentTime(v) {} get duration() { return 0; } get volume() { return 1; } set volume(v) {}
  get complete() { return true; } get naturalWidth() { return 0; } get naturalHeight() { return 0; }
}
for (const b of BOOL) Object.defineProperty(Element.prototype, b, { get() { return this.hasAttribute(b.toLowerCase()); }, set(v) { v ? this.setAttribute(b.toLowerCase(), '') : this.removeAttribute(b.toLowerCase()); }, configurable: true });
for (const s of STR) Object.defineProperty(Element.prototype, s, { get() { const v = this.getAttribute(s.toLowerCase()); return v === null ? '' : v; }, set(v) { this.setAttribute(s.toLowerCase(), v); }, configurable: true });

// ---------------------------------------------------------------- document
const docId = D.doc();
class Document extends Node {
  constructor() { super(docId); this.__cookies = {}; this.__ready = 'loading'; }
  get documentElement() { return wrap(D.html()); }
  get head() { return wrap(D.head()); }
  get body() { return wrap(D.body()); }
  get title() { const t = this.querySelector('title'); return t ? t.textContent.trim() : ''; }
  set title(v) { let t = this.querySelector('title'); if (!t) { t = this.createElement('title'); this.head.appendChild(t); } t.textContent = v; }
  get readyState() { return this.__ready; }
  get URL() { return G.location.href; } get documentURI() { return G.location.href; } get baseURI() { return G.location.href; }
  get domain() { return G.location.hostname; } get referrer() { return ''; } get characterSet() { return 'UTF-8'; } get contentType() { return 'text/html'; }
  get compatMode() { return 'CSS1Compat'; } get hidden() { return false; } get visibilityState() { return 'visible'; }
  get activeElement() { return this.body; } get scrollingElement() { return this.documentElement; } get defaultView() { return G; }
  get currentScript() { return wrap(D.currentScript()); }
  get cookie() { return Object.keys(this.__cookies).map(k => k + '=' + this.__cookies[k]).join('; '); }
  set cookie(v) { const p = String(v).split(';')[0]; const i = p.indexOf('='); if (i > 0) this.__cookies[p.slice(0, i).trim()] = p.slice(i + 1).trim(); }
  get forms() { return this.querySelectorAll('form'); } get images() { return this.querySelectorAll('img'); } get links() { return this.querySelectorAll('a[href]'); } get scripts() { return this.querySelectorAll('script'); }
  get styleSheets() { return []; } get fonts() { return { ready: Promise.resolve(), load() { return Promise.resolve([]); }, check() { return true; }, addEventListener() {} }; }
  hasFocus() { return true; }
  getElementById(i) { const x = D.byId(String(i)); return x < 0 ? null : wrap(x); }
  getElementsByTagName(t) { return qsAll(docId, String(t)); }
  getElementsByClassName(c) { return qsAll(docId, String(c).trim().split(/\s+/).map(x => '.' + x).join('')); }
  getElementsByName(n) { return qsAll(docId, '[name="' + n + '"]'); }
  querySelector(s) { return qsOne(docId, s); } querySelectorAll(s) { return qsAll(docId, s); }
  createElement(t) { return wrap(D.create(String(t))); } createElementNS(ns, t) { return wrap(D.create(String(t))); }
  createTextNode(s) { return wrap(D.createText(String(s))); } createComment(s) { return wrap(D.createText('')); }
  createDocumentFragment() { return wrap(D.createFrag()); }
  createEvent(t) { return new Event(''); } createRange() { return { setStart() {}, setEnd() {}, selectNodeContents() {}, collapse() {}, getBoundingClientRect() { return { top: 0, left: 0, width: 0, height: 0 }; }, createContextualFragment: h => { const f = G.document.createDocumentFragment(); const d = G.document.createElement('div'); d.innerHTML = h; while (d.firstChild) f.appendChild(d.firstChild); return f; } }; }
  importNode(n, d) { return n.cloneNode(!!d); } adoptNode(n) { return n; }
  elementFromPoint() { return null; } elementsFromPoint() { return []; }
  getSelection() { return { removeAllRanges() {}, addRange() {}, toString() { return ''; }, rangeCount: 0 }; }
  execCommand() { return false; } queryCommandSupported() { return false; }
  write(h) { D.write(String(h)); } writeln(h) { D.write(String(h) + '\n'); } open() { return this; } close() {}
}
const document = new Document();
cache.set(docId, document);
G.document = document;
G.Node = Node; G.Element = Element; G.HTMLElement = Element; G.Text = Text; G.Comment = Comment; G.Document = Document; G.DocumentFragment = DocumentFragment;
G.EventTarget = EventTarget; G.Event = Event; G.CustomEvent = CustomEvent; G.MouseEvent = MouseEvent; G.KeyboardEvent = KeyboardEvent; G.UIEvent = UIEvent; G.DOMException = DOMException;
for (const n of ['HTMLDivElement', 'HTMLSpanElement', 'HTMLAnchorElement', 'HTMLImageElement', 'HTMLInputElement', 'HTMLButtonElement', 'HTMLFormElement', 'HTMLCanvasElement', 'HTMLScriptElement', 'HTMLStyleElement', 'HTMLVideoElement', 'HTMLAudioElement', 'HTMLSelectElement', 'HTMLTextAreaElement', 'HTMLBodyElement', 'HTMLIFrameElement', 'SVGElement']) G[n] = Element;

// ---------------------------------------------------------------- window
G.window = G; G.self = G; G.top = G; G.parent = G; G.frames = G; G.globalThis = G;
Object.defineProperty(G, '__parentTarget', { value: () => null, enumerable: false });
for (const k of ['addEventListener', 'removeEventListener', 'dispatchEvent']) Object.defineProperty(G, k, { value: EventTarget.prototype[k], enumerable: false, writable: true, configurable: true });
const fmt = a => { if (typeof a === 'string') return a; if (a instanceof Error) return a.name + ': ' + a.message + (a.stack ? '\n' + a.stack : ''); try { if (a && typeof a === 'object') { const seen = new Set(); return JSON.stringify(a, (k, v) => { if (typeof v === 'object' && v) { if (seen.has(v)) return '[circular]'; seen.add(v); } if (typeof v === 'function') return '[fn]'; return v; }); } } catch (e) {} return String(a); };
const mk = lv => (...a) => N.log(lv, a.map(fmt).join(' '));
G.console = { log: mk(0), info: mk(0), debug: mk(0), warn: mk(1), error: mk(2), trace: mk(0), assert: (c, ...a) => { if (!c) N.log(2, 'Assertion failed ' + a.map(fmt).join(' ')); }, dir: mk(0), table: mk(0), group() {}, groupEnd() {}, time() {}, timeEnd() {}, clear() {}, count() {} };
N.error = e => N.log(2, 'Uncaught ' + fmt(e));

G.setTimeout = (fn, ms, ...a) => typeof fn === 'function' ? N.setTimer(() => fn(...a), ms | 0, false) : 0;
G.setInterval = (fn, ms, ...a) => typeof fn === 'function' ? N.setTimer(() => fn(...a), Math.max(ms | 0, 4), true) : 0;
G.clearTimeout = id => N.clearTimer(id | 0); G.clearInterval = G.clearTimeout;
G.requestAnimationFrame = fn => N.raf(fn); G.cancelAnimationFrame = id => N.clearTimer(id | 0);
G.requestIdleCallback = fn => G.setTimeout(() => fn({ didTimeout: false, timeRemaining: () => 10 }), 1); G.cancelIdleCallback = G.clearTimeout;
G.queueMicrotask = fn => { Promise.resolve().then(fn); };
G.structuredClone = v => JSON.parse(JSON.stringify(v));
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
G.btoa = s => { s = String(s); let o = ''; for (let i = 0; i < s.length; i += 3) { const a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2); o += B64[a >> 2] + B64[((a & 3) << 4) | (b >> 4 || 0)] + (isNaN(b) ? '=' : B64[((b & 15) << 2) | (c >> 6 || 0)]) + (isNaN(c) ? '=' : B64[c & 63]); } return o; };
G.atob = s => { s = String(s).replace(/[^A-Za-z0-9+/]/g, ''); let o = ''; for (let i = 0; i < s.length; i += 4) { const a = B64.indexOf(s[i]), b = B64.indexOf(s[i + 1]), c = B64.indexOf(s[i + 2]), d = B64.indexOf(s[i + 3]); o += String.fromCharCode((a << 2) | (b >> 4)); if (c >= 0) o += String.fromCharCode(((b & 15) << 4) | (c >> 2)); if (d >= 0) o += String.fromCharCode(((c & 3) << 6) | d); } return o; };
G.alert = m => N.log(0, '[alert] ' + m); G.confirm = () => false; G.prompt = () => null; G.open = () => null; G.close = () => {}; G.focus = () => {}; G.blur = () => {}; G.print = () => {};
G.scrollTo = G.scroll = G.scrollBy = () => {};
G.innerWidth = 1280; G.innerHeight = 720; G.outerWidth = 1280; G.outerHeight = 720; G.devicePixelRatio = 1; G.scrollX = G.scrollY = G.pageXOffset = G.pageYOffset = 0;
G.screen = { width: 1920, height: 1080, availWidth: 1920, availHeight: 1080, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0 } };
G.navigator = { userAgent: N.ua(), language: 'es', languages: ['es', 'en'], platform: 'PlayStation 4', vendor: '', cookieEnabled: true, onLine: true, hardwareConcurrency: 6, maxTouchPoints: 0, appName: 'Netscape', appVersion: '5.0', product: 'Gecko', doNotTrack: null,
  sendBeacon: () => true, clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve('') }, getGamepads: () => [], serviceWorker: undefined, mediaDevices: undefined, permissions: { query: () => Promise.resolve({ state: 'denied' }) }, vibrate: () => false };
G.performance = { now: () => N.now(), timeOrigin: 0, mark() {}, measure() {}, getEntriesByName: () => [], getEntriesByType: () => [], timing: {} };
G.matchMedia = q => ({ matches: false, media: String(q), onchange: null, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} });
G.getComputedStyle = el => { const st = el && el.style ? el.style : {}; return new Proxy({}, { get: (t, p) => p === 'getPropertyValue' ? (k => st.getPropertyValue ? st.getPropertyValue(k) : '') : (p === 'display' ? (st[p] || 'block') : (st[p] !== undefined ? st[p] : '')) }); };
G.history = { length: 1, state: null, pushState(s) { this.state = s; }, replaceState(s) { this.state = s; }, back() {}, forward() {}, go() {}, scrollRestoration: 'auto' };
class Observer { constructor(cb) { this.__cb = cb; } observe() {} unobserve() {} disconnect() {} takeRecords() { return []; } }
G.MutationObserver = Observer; G.ResizeObserver = Observer; G.IntersectionObserver = Observer; G.PerformanceObserver = Observer;
G.customElements = { define() {}, get() { return undefined; }, whenDefined() { return Promise.resolve(); } };
G.CSS = { supports: () => false, escape: s => String(s) };
G.Image = function (w, h) { const i = document.createElement('img'); if (w) i.setAttribute('width', w); if (h) i.setAttribute('height', h); return i; };
G.Audio = function (src) { const a = document.createElement('audio'); if (src) a.setAttribute('src', src); return a; };
G.AudioContext = G.webkitAudioContext = undefined;
G.Option = function (t, v) { const o = document.createElement('option'); if (t) o.textContent = t; if (v !== undefined) o.setAttribute('value', v); return o; };

// Storage
class Storage { constructor() { Object.defineProperty(this, '__m', { value: new Map() }); }
  getItem(k) { return this.__m.has(String(k)) ? this.__m.get(String(k)) : null; } setItem(k, v) { this.__m.set(String(k), String(v)); } removeItem(k) { this.__m.delete(String(k)); } clear() { this.__m.clear(); }
  key(i) { return [...this.__m.keys()][i] ?? null; } get length() { return this.__m.size; } }
G.localStorage = new Storage(); G.sessionStorage = new Storage(); G.Storage = Storage;

// URL / URLSearchParams
class URLSearchParams {
  constructor(init) { this.__a = []; if (typeof init === 'string') { init.replace(/^\?/, '').split('&').forEach(p => { if (!p) return; const i = p.indexOf('='); const d = s => decodeURIComponent(s.replace(/\+/g, ' ')); this.__a.push(i < 0 ? [d(p), ''] : [d(p.slice(0, i)), d(p.slice(i + 1))]); }); } else if (init && typeof init === 'object') { for (const k of Object.keys(init)) this.__a.push([k, String(init[k])]); } }
  get(k) { const e = this.__a.find(x => x[0] === k); return e ? e[1] : null; } getAll(k) { return this.__a.filter(x => x[0] === k).map(x => x[1]); } has(k) { return this.__a.some(x => x[0] === k); }
  set(k, v) { const i = this.__a.findIndex(x => x[0] === k); if (i < 0) this.__a.push([k, String(v)]); else { this.__a[i][1] = String(v); this.__a = this.__a.filter((x, j) => x[0] !== k || j === i); } }
  append(k, v) { this.__a.push([k, String(v)]); } delete(k) { this.__a = this.__a.filter(x => x[0] !== k); } forEach(f) { this.__a.forEach(x => f(x[1], x[0])); }
  keys() { return this.__a.map(x => x[0])[Symbol.iterator](); } values() { return this.__a.map(x => x[1])[Symbol.iterator](); } entries() { return this.__a.map(x => [x[0], x[1]])[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); }
  toString() { return this.__a.map(x => encodeURIComponent(x[0]) + '=' + encodeURIComponent(x[1])).join('&'); }
}
class URL {
  constructor(u, base) {
    u = String(u);
    if (!/^[a-z][a-z0-9+.-]*:/i.test(u)) { if (base === undefined) throw new TypeError('Invalid URL'); u = N.resolve(String(base), u); }
    const m = /^([a-z][a-z0-9+.-]*:)\/\/([^\/?#]*)([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(u);
    if (!m) throw new TypeError('Invalid URL');
    this.protocol = m[1].toLowerCase(); this.host = m[2].replace(/^.*@/, ''); this.hostname = this.host.replace(/:\d+$/, ''); this.port = (/:(\d+)$/.exec(this.host) || [, ''])[1];
    this.pathname = m[3] || '/'; this.search = m[4] && m[4] !== '?' ? m[4] : ''; this.hash = m[5] && m[5] !== '#' ? m[5] : ''; this.origin = this.protocol + '//' + this.host; this.username = ''; this.password = '';
    this.searchParams = new URLSearchParams(this.search);
  }
  get href() { const q = this.searchParams.toString(); return this.origin + this.pathname + (q ? '?' + q : '') + this.hash; }
  toString() { return this.href; } toJSON() { return this.href; }
}
G.URL = URL; G.URLSearchParams = URLSearchParams; URL.createObjectURL = () => 'blob:psb'; URL.revokeObjectURL = () => {};

// location
(function () {
  const href0 = N.url();
  let u; try { u = new URL(href0); } catch (e) { u = null; }
  G.location = { href: href0, origin: u ? u.origin : '', protocol: u ? u.protocol : 'https:', host: u ? u.host : '', hostname: u ? u.hostname : '', port: u ? u.port : '', pathname: u ? u.pathname : '/', search: u ? u.search : '', hash: u ? u.hash : '',
    assign() {}, replace() {}, reload() {}, toString() { return this.href; } };
})();


// fetch / XHR
class Headers { constructor(i) { this.__m = {}; if (i) for (const k of Object.keys(i)) this.__m[k.toLowerCase()] = String(i[k]); } get(k) { const v = this.__m[String(k).toLowerCase()]; return v === undefined ? null : v; } has(k) { return String(k).toLowerCase() in this.__m; } set(k, v) { this.__m[String(k).toLowerCase()] = String(v); } append(k, v) { this.set(k, v); } forEach(f) { for (const k in this.__m) f(this.__m[k], k); } }
class Response {
  constructor(body, init) { init = init || {}; this.__b = body == null ? '' : String(body); this.status = init.status === undefined ? 200 : init.status; this.ok = this.status >= 200 && this.status < 300; this.statusText = ''; this.url = init.url || ''; this.headers = new Headers(init.headers); this.type = 'basic'; this.redirected = false; this.bodyUsed = false; }
  text() { return Promise.resolve(this.__b); } json() { try { return Promise.resolve(JSON.parse(this.__b)); } catch (e) { return Promise.reject(e); } }
  arrayBuffer() { const s = this.__b, b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 255; return Promise.resolve(b.buffer); } blob() { return Promise.resolve({ size: this.__b.length, type: this.headers.get('content-type') || '', text: () => Promise.resolve(this.__b) }); }
  clone() { return new Response(this.__b, { status: this.status, url: this.url, headers: this.headers.__m }); }
}
G.Headers = Headers; G.Response = Response; G.Request = function (u, o) { return Object.assign({ url: String(u) }, o); };
G.fetch = (input, opts) => new Promise((res, rej) => {
  const url = typeof input === 'string' ? input : (input && (input.url || String(input)));
  const r = N.fetchSync(String(url), (opts && opts.method) || 'GET');
  if (!r) return rej(new TypeError('Failed to fetch'));
  res(new Response(r.body, { status: r.status, url: r.url, headers: { 'content-type': r.type } }));
});
G.XMLHttpRequest = class XMLHttpRequest extends EventTarget {
  constructor() { super(); this.readyState = 0; this.status = 0; this.responseText = ''; this.response = null; this.responseType = ''; this.responseURL = ''; this.onload = this.onerror = this.onreadystatechange = this.onloadend = null; this.__h = {}; }
  open(m, u) { this.__m = m; this.__u = String(u); this.readyState = 1; }
  setRequestHeader() {} overrideMimeType() {} abort() {}
  getResponseHeader(k) { return String(k).toLowerCase() === 'content-type' ? this.__t || null : null; } getAllResponseHeaders() { return this.__t ? 'content-type: ' + this.__t + '\r\n' : ''; }
  send() {
    const r = N.fetchSync(this.__u, this.__m || 'GET');
    G.setTimeout(() => {
      if (!r) { this.readyState = 4; this.status = 0; ['onerror', 'onloadend'].forEach(h => this[h] && this[h]({ type: h.slice(2) })); this.dispatchEvent(new Event('error')); return; }
      this.readyState = 4; this.status = r.status; this.responseText = r.body; this.responseURL = r.url; this.__t = r.type;
      this.response = this.responseType === 'json' ? (() => { try { return JSON.parse(r.body); } catch (e) { return null; } })() : r.body;
      if (this.onreadystatechange) this.onreadystatechange({ type: 'readystatechange' });
      if (this.onload) this.onload({ type: 'load', target: this });
      this.dispatchEvent(new Event('load')); this.dispatchEvent(new Event('readystatechange')); if (this.onloadend) this.onloadend({ type: 'loadend' });
    }, 0);
  }
};
G.FormData = class FormData { constructor() { this.__a = []; } append(k, v) { this.__a.push([k, v]); } get(k) { const e = this.__a.find(x => x[0] === k); return e ? e[1] : null; } };
G.Blob = class Blob { constructor(p, o) { this.__p = p || []; this.size = this.__p.join('').length; this.type = (o && o.type) || ''; } text() { return Promise.resolve(this.__p.join('')); } };
G.AbortController = class AbortController { constructor() { this.signal = { aborted: false, addEventListener() {}, removeEventListener() {} }; } abort() { this.signal.aborted = true; } };

// eventos de ciclo de vida (los llama el nativo)
G.__fire = (target, type) => { const ev = new Event(type, { bubbles: false }); (target === 'window' ? G : document).dispatchEvent(ev); };
G.__setReady = s => { document.__ready = s; document.dispatchEvent(new Event('readystatechange')); };
G.__click = id => { const el = wrap(id); if (el) el.click(); };
})(globalThis);
)JS";
