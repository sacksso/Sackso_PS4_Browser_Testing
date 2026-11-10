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
  initEvent(t, b, c) { this.type = String(t); this.bubbles = !!b; this.cancelable = !!c; }
  initCustomEvent(t, b, c, d) { this.initEvent(t, b, c); this.detail = d === undefined ? null : d; }
  initUIEvent(t, b, c) { this.initEvent(t, b, c); }
  initMouseEvent(t, b, c) { this.initEvent(t, b, c); }
  initKeyboardEvent(t, b, c) { this.initEvent(t, b, c); }
  get returnValue() { return !this.defaultPrevented; } set returnValue(v) { if (!v) this.preventDefault(); }
  get srcElement() { return this.target; } get eventPhase() { return 2; } get composed() { return false; }
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
  compareDocumentPosition(o) {
    if (this === o) return 0;
    if (!o) return 1;
    if (this.contains(o)) return 20;   // CONTAINED_BY | FOLLOWING
    if (o.contains(this)) return 10;   // CONTAINS | PRECEDING
    const pa = [], pb = [];
    for (let n = this; n; n = n.parentNode) pa.push(n);
    for (let n = o; n; n = n.parentNode) pb.push(n);
    pa.reverse(); pb.reverse();
    if (pa[0] !== pb[0]) return 1 | 32 | (idOf(this) < idOf(o) ? 4 : 2);   // DISCONNECTED | IMPLEMENTATION_SPECIFIC
    let i = 0; while (i < pa.length && i < pb.length && pa[i] === pb[i]) i++;
    for (let s = pa[i]; s; s = s.nextSibling) if (s === pb[i]) return 4;
    return 2;
  }
  hasAttributes() { return this.nodeType === 1 && D.attrNames(this.__id).length > 0; }
  lookupNamespaceURI() { return null; } lookupPrefix() { return null; } isDefaultNamespace() { return true; }
  get baseURI() { return G.location.href; }
  get prefix() { return null; } get namespaceURI() { return this.nodeType === 1 ? 'http://www.w3.org/1999/xhtml' : null; }
}
for (const [k, v] of Object.entries({ DOCUMENT_POSITION_DISCONNECTED: 1, DOCUMENT_POSITION_PRECEDING: 2, DOCUMENT_POSITION_FOLLOWING: 4, DOCUMENT_POSITION_CONTAINS: 8, DOCUMENT_POSITION_CONTAINED_BY: 16, DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC: 32 })) { Node[k] = v; Node.prototype[k] = v; }
Node.ATTRIBUTE_NODE = 2; Node.CDATA_SECTION_NODE = 4; Node.PROCESSING_INSTRUCTION_NODE = 7; Node.DOCUMENT_TYPE_NODE = 10;
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
  get attributes() { const a = D.attrNames(this.__id).map(n => ({ name: n, value: D.getAttr(this.__id, n), nodeName: n, nodeValue: D.getAttr(this.__id, n), specified: true, expando: false })); for (const x of a) if (!(x.name in a)) a[x.name] = x; a.getNamedItem = k => a[String(k).toLowerCase()] || null; a.item = i => a[i] || null; return a; }
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
  webkitMatchesSelector(s) { return this.matches(s); } msMatchesSelector(s) { return this.matches(s); } mozMatchesSelector(s) { return this.matches(s); }
  getAttributeNode(n) { const v = this.getAttribute(n); return v === null ? null : { name: String(n), value: v, nodeName: String(n), nodeValue: v, specified: true }; }
  get content() { if (this.localName !== 'template') return undefined; if (!this.__content) { const f = G.document.createDocumentFragment(); for (const k of this.childNodes) f.appendChild(k.cloneNode(true)); Object.defineProperty(this, '__content', { value: f, enumerable: false }); } return this.__content; }
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
  get cookie() { const r = N.cookieGet(); if (r !== null) return r; return Object.keys(this.__cookies).map(k => k + '=' + this.__cookies[k]).join('; '); }
  set cookie(v) { if (N.cookieGet() !== null) { N.cookieSet(String(v)); return; } const p = String(v).split(';')[0]; const i = p.indexOf('='); if (i > 0) this.__cookies[p.slice(0, i).trim()] = p.slice(i + 1).trim(); }
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
  createEvent(t) { const k = String(t || 'Event').toLowerCase(); return k.indexOf('custom') >= 0 ? new CustomEvent('') : k.indexOf('mouse') >= 0 ? new MouseEvent('') : k.indexOf('key') >= 0 ? new KeyboardEvent('') : new Event(''); } createRange() { return { setStart() {}, setEnd() {}, selectNodeContents() {}, collapse() {}, getBoundingClientRect() { return { top: 0, left: 0, width: 0, height: 0 }; }, createContextualFragment: h => { const f = G.document.createDocumentFragment(); const d = G.document.createElement('div'); d.innerHTML = h; while (d.firstChild) f.appendChild(d.firstChild); return f; } }; }
  importNode(n, d) { return n.cloneNode(!!d); } adoptNode(n) { return n; }
  elementFromPoint() { return null; } elementsFromPoint() { return []; }
  getSelection() { return { removeAllRanges() {}, addRange() {}, toString() { return ''; }, rangeCount: 0 }; }
  execCommand() { return false; } queryCommandSupported() { return false; }
  write(h) { D.write(String(h)); } writeln(h) { D.write(String(h) + '\n'); } open() { return this; } close() {}
}
const document = new Document();
cache.set(docId, document);
G.document = document;

// ---- compatibilidad v0.9.1: documentos desligados (jQuery, DOMParser), globales que piden las bibliotecas habituales
class HTMLDocumentLite {
  constructor(title) {
    const html = document.createElement('html'), head = document.createElement('head'), body = document.createElement('body');
    html.appendChild(head); html.appendChild(body);
    if (title) { const t = document.createElement('title'); t.textContent = String(title); head.appendChild(t); }
    Object.defineProperty(this, '__html', { value: html });
    this.documentElement = html; this.head = head; this.body = body;
    this.readyState = 'complete'; this.contentType = 'text/html'; this.compatMode = 'CSS1Compat'; this.characterSet = 'UTF-8';
  }
  get nodeType() { return 9; } get nodeName() { return '#document'; } get ownerDocument() { return null; } get defaultView() { return null; }
  get location() { return G.location; } get URL() { return G.location.href; } get baseURI() { return G.location.href; }
  get childNodes() { return [this.__html]; } get firstChild() { return this.__html; } get children() { return [this.__html]; }
  get title() { const t = this.__html.querySelector('title'); return t ? t.textContent : ''; }
  createElement(t) { return document.createElement(t); } createElementNS(n, t) { return document.createElement(t); }
  createTextNode(s) { return document.createTextNode(s); } createComment(s) { return document.createComment(s); } createDocumentFragment() { return document.createDocumentFragment(); }
  getElementById(i) { return this.__html.querySelector('[id="' + String(i).replace(/"/g, '') + '"]'); }
  getElementsByTagName(t) { return this.__html.querySelectorAll(t); } getElementsByClassName(c) { return this.__html.querySelectorAll(String(c).trim().split(/\s+/).map(x => '.' + x).join('')); }
  querySelector(s) { return this.__html.querySelector(s); } querySelectorAll(s) { return this.__html.querySelectorAll(s); }
  contains(n) { return this.__html.contains(n); } importNode(n, d) { return n.cloneNode(!!d); } adoptNode(n) { return n; }
  addEventListener() {} removeEventListener() {} dispatchEvent() { return true; } hasFocus() { return false; }
}
const DOMIMPL = { createHTMLDocument: t => new HTMLDocumentLite(t), createDocument: () => new HTMLDocumentLite(''), createDocumentType: () => ({}), hasFeature: () => true };
Object.defineProperty(Document.prototype, 'implementation', { get() { return DOMIMPL; }, configurable: true });
Object.defineProperty(HTMLDocumentLite.prototype, 'implementation', { get() { return DOMIMPL; }, configurable: true });
G.DOMParser = class DOMParser { parseFromString(s, t) { const d = new HTMLDocumentLite(''); d.body.innerHTML = String(s); return d; } };
class NodeList extends Array { static [Symbol.hasInstance](o) { return Array.isArray(o); } }
G.NodeList = NodeList; G.HTMLCollection = NodeList; G.NamedNodeMap = NodeList;
G.ShadowRoot = class ShadowRoot extends DocumentFragment {};
Object.defineProperty(Element.prototype, 'shadowRoot', { get() { return null; }, configurable: true });
G.MediaError = class MediaError { constructor() { this.code = 0; this.message = ''; } };
Object.assign(G.MediaError, { MEDIA_ERR_ABORTED: 1, MEDIA_ERR_NETWORK: 2, MEDIA_ERR_DECODE: 3, MEDIA_ERR_SRC_NOT_SUPPORTED: 4 });
for (const n of ['CSSStyleDeclaration', 'DOMTokenList', 'Range', 'Selection', 'DOMRect', 'TimeRanges', 'TextTrack', 'MediaSource', 'SourceBuffer', 'RTCPeerConnection', 'MediaStream', 'Notification']) if (!G[n]) G[n] = ({ [n]: class {} })[n];
G.Notification = undefined; G.MediaSource = undefined; G.RTCPeerConnection = undefined;  // las bibliotecas comprueban su existencia para decidir si las usan
for (const n of ['PointerEvent', 'WheelEvent', 'TouchEvent', 'FocusEvent', 'InputEvent', 'ErrorEvent', 'MessageEvent', 'HashChangeEvent', 'PopStateEvent', 'ProgressEvent', 'TransitionEvent', 'AnimationEvent', 'ClipboardEvent', 'DragEvent', 'StorageEvent']) if (!G[n]) G[n] = ({ [n]: class extends MouseEvent {} })[n];
G.crypto = {
  getRandomValues(a) { const m = Math.pow(2, 8 * (a.BYTES_PER_ELEMENT || 1)); for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * m); return a; },
  randomUUID() { return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, c => { const r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }); },
  subtle: undefined };
G.TextEncoder = class TextEncoder { get encoding() { return 'utf-8'; } encode(s) { const e = unescape(encodeURIComponent(String(s === undefined ? '' : s))); const u = new Uint8Array(e.length); for (let i = 0; i < e.length; i++) u[i] = e.charCodeAt(i); return u; } };
G.TextDecoder = class TextDecoder { constructor(l) { this.encoding = l || 'utf-8'; } decode(b) { if (!b) return ''; const u = b instanceof ArrayBuffer ? new Uint8Array(b) : new Uint8Array(b.buffer || b, b.byteOffset || 0, b.byteLength); let s = ''; for (let i = 0; i < u.length; i++) s += String.fromCharCode(u[i]); try { return decodeURIComponent(escape(s)); } catch (e) { return s; } } };
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
G.requestAnimationFrame = fn => typeof fn === 'function' ? N.raf(() => fn(N.now())) : 0; G.cancelAnimationFrame = id => N.clearTimer(id | 0);
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
class Headers { constructor(i) { this.__m = {}; if (i) { if (i instanceof Headers) Object.assign(this.__m, i.__m); else if (Array.isArray(i)) { for (const p of i) if (p && p.length >= 2) this.__m[String(p[0]).toLowerCase()] = String(p[1]); } else for (const k of Object.keys(i)) this.__m[k.toLowerCase()] = String(i[k]); } } get(k) { const v = this.__m[String(k).toLowerCase()]; return v === undefined ? null : v; } has(k) { return String(k).toLowerCase() in this.__m; } set(k, v) { this.__m[String(k).toLowerCase()] = String(v); } append(k, v) { this.set(k, v); } forEach(f) { for (const k in this.__m) f(this.__m[k], k); } }
class Response {
  constructor(body, init) { init = init || {}; this.__b = body == null ? '' : String(body); this.status = init.status === undefined ? 200 : init.status; this.ok = this.status >= 200 && this.status < 300; this.statusText = ''; this.url = init.url || ''; this.headers = new Headers(init.headers); this.type = 'basic'; this.redirected = false; this.bodyUsed = false; }
  text() { return Promise.resolve(this.__b); } json() { try { return Promise.resolve(JSON.parse(this.__b)); } catch (e) { return Promise.reject(e); } }
  arrayBuffer() { const s = this.__b, b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 255; return Promise.resolve(b.buffer); } blob() { return Promise.resolve({ size: this.__b.length, type: this.headers.get('content-type') || '', text: () => Promise.resolve(this.__b) }); }
  clone() { return new Response(this.__b, { status: this.status, url: this.url, headers: this.headers.__m }); }
}
const hdrText = m => { let t = ''; for (const k in m) { const v = String(m[k]).replace(/[\r\n]+/g, ' '); if (/^[A-Za-z0-9_-]+$/.test(k)) t += k + ': ' + v + '\n'; } return t; };
G.Headers = Headers; G.Response = Response; G.Request = class Request { constructor(u, o) { o = o || {}; const src = (u && typeof u === 'object') ? u : null; this.url = String(src ? src.url : u); this.method = String(o.method || (src && src.method) || 'GET').toUpperCase(); this.headers = new Headers(o.headers || (src && src.headers)); this.body = o.body !== undefined ? o.body : (src ? src.body : null); this.credentials = o.credentials || 'same-origin'; this.mode = o.mode || 'cors'; this.cache = 'default'; this.redirect = o.redirect || 'follow'; this.referrer = ''; this.signal = o.signal || null; }
  clone() { return new Request(this); } text() { return Promise.resolve(this.body == null ? '' : String(this.body)); } };
G.fetch = (input, opts) => new Promise((res, rej) => {
  const url = typeof input === 'string' ? input : (input && (input.url || String(input)));
  const o = opts || (input && typeof input === 'object' ? input : {});
  let hct = null, htxt = ''; if (o.headers) { const hh = o.headers instanceof Headers ? o.headers : new Headers(o.headers); hct = hh.get('content-type'); htxt = hdrText(hh.__m); }
  let body = o.body; if (body && typeof body === 'object') { if (typeof body.toString === 'function' && body instanceof URLSearchParams) { body = body.toString(); hct = hct || 'application/x-www-form-urlencoded'; } else if (body instanceof FormData) { body = body.__a.map(x => encodeURIComponent(x[0]) + '=' + encodeURIComponent(x[1])).join('&'); hct = hct || 'application/x-www-form-urlencoded'; } else body = String(body); }
  const r = N.fetchSync(String(url), String(o.method || 'GET').toUpperCase(), body === undefined ? undefined : String(body), hct || undefined, htxt || undefined);
  if (!r) return rej(new TypeError('Failed to fetch'));
  res(new Response(r.body, { status: r.status, url: r.url, headers: { 'content-type': r.type } }));
});
G.XMLHttpRequest = class XMLHttpRequest extends EventTarget {
  constructor() { super(); this.readyState = 0; this.status = 0; this.responseText = ''; this.response = null; this.responseType = ''; this.responseURL = ''; this.onload = this.onerror = this.onreadystatechange = this.onloadend = this.onabort = this.ontimeout = this.onprogress = null; this.__h = {}; this.__hd = {}; this.upload = new EventTarget(); this.statusText = ''; this.timeout = 0; this.withCredentials = false; }
  open(m, u) { this.__m = String(m).toUpperCase(); this.__u = String(u); this.readyState = 1; }
  setRequestHeader(k, v) { k = String(k); if (k.toLowerCase() === 'content-type') this.__ct = String(v); else this.__hd[k] = (k in this.__hd ? this.__hd[k] + ', ' : '') + String(v); } overrideMimeType() {} abort() {}
  getResponseHeader(k) { return String(k).toLowerCase() === 'content-type' ? this.__t || null : null; } getAllResponseHeaders() { return this.__t ? 'content-type: ' + this.__t + '\r\n' : ''; }
  send(body) {
    if (body && typeof body === 'object') body = body instanceof URLSearchParams ? body.toString() : (body instanceof FormData ? body.__a.map(x => encodeURIComponent(x[0]) + '=' + encodeURIComponent(x[1])).join('&') : String(body));
    const r = N.fetchSync(this.__u, this.__m || 'GET', body === undefined || body === null ? undefined : String(body), this.__ct, hdrText(this.__hd) || undefined);
    G.setTimeout(() => {
      if (!r) { this.readyState = 4; this.status = 0; ['onerror', 'onloadend'].forEach(h => this[h] && this[h]({ type: h.slice(2) })); this.dispatchEvent(new Event('error')); return; }
      this.readyState = 4; this.status = r.status; this.statusText = r.status === 200 ? 'OK' : ''; this.responseText = r.body; this.responseURL = r.url; this.__t = r.type;
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

// ---------------------------------------------------------------- v0.9.4: APIs que faltaban (stubs que fallan limpio)
// Una pagina que usa WebSocket/IndexedDB/Intl sin comprobar antes lanzaba ReferenceError y abortaba el script entero.
(function () {
  const def = (n, v) => { if (typeof G[n] === 'undefined') G[n] = v; };
  const later = f => setTimeout(f, 0);
  const emit = (t, type, extra) => { const e = new Event(type); if (extra) Object.assign(e, extra); try { t.dispatchEvent(e); } catch (x) {} const h = t['on' + type]; if (typeof h === 'function') { try { h.call(t, e); } catch (x) {} } };
  // WebSocket / EventSource: no hay sockets; la conexion "falla" enseguida para que la pagina use su alternativa (polling)
  def('WebSocket', class WebSocket extends EventTarget {
    constructor(u) { super(); this.url = String(u); this.readyState = 0; this.protocol = ''; this.extensions = ''; this.bufferedAmount = 0; this.binaryType = 'blob'; this.onopen = this.onmessage = this.onerror = this.onclose = null;
      later(() => { this.readyState = 3; emit(this, 'error'); emit(this, 'close', { code: 1006, reason: '', wasClean: false }); }); }
    send() { throw new Error('WebSocket is not open'); } close() { this.readyState = 3; }
  });
  G.WebSocket.CONNECTING = 0; G.WebSocket.OPEN = 1; G.WebSocket.CLOSING = 2; G.WebSocket.CLOSED = 3;
  def('EventSource', class EventSource extends EventTarget {
    constructor(u) { super(); this.url = String(u); this.readyState = 0; this.onopen = this.onmessage = this.onerror = null; later(() => { this.readyState = 2; emit(this, 'error'); }); }
    close() { this.readyState = 2; }
  });
  // BroadcastChannel solo dentro de la misma pagina
  const bc = {};
  def('BroadcastChannel', class BroadcastChannel extends EventTarget {
    constructor(n) { super(); this.name = String(n); this.onmessage = null; (bc[this.name] = bc[this.name] || []).push(this); }
    postMessage(d) { for (const c of bc[this.name]) if (c !== this) later(() => emit(c, 'message', { data: d })); }
    close() { const a = bc[this.name] || []; const i = a.indexOf(this); if (i >= 0) a.splice(i, 1); }
  });
  // IndexedDB: abrir falla con un error normal; las librerias suelen volver a localStorage
  def('indexedDB', { open() { const r = new EventTarget(); r.onerror = r.onsuccess = r.onupgradeneeded = r.onblocked = null; r.error = new Error('IndexedDB no disponible'); r.readyState = 'done';
      later(() => emit(r, 'error')); return r; }, deleteDatabase() { return this.open(); }, databases() { return Promise.resolve([]); }, cmp(a, b) { return a < b ? -1 : a > b ? 1 : 0; } });
  // File / FileReader (FileReader no lee archivos reales: falla con onerror)
  if (typeof G.Blob !== 'undefined') def('File', class File extends G.Blob { constructor(p, n, o) { super(p, o); this.name = String(n); this.lastModified = (o && o.lastModified) || Date.now(); } });
  def('FileReader', class FileReader extends EventTarget {
    constructor() { super(); this.readyState = 0; this.result = null; this.error = null; this.onload = this.onerror = this.onloadend = null; }
    __fail() { later(() => { this.readyState = 2; this.error = new Error('FileReader no disponible'); emit(this, 'error'); emit(this, 'loadend'); }); }
    readAsText() { this.__fail(); } readAsDataURL() { this.__fail(); } readAsArrayBuffer() { this.__fail(); } readAsBinaryString() { this.__fail(); } abort() {}
  });
  // ReadableStream minimo (response.body.getReader() termina enseguida)
  def('ReadableStream', class ReadableStream { constructor() {} getReader() { return { read: () => Promise.resolve({ done: true, value: undefined }), releaseLock() {}, cancel: () => Promise.resolve(), closed: Promise.resolve() }; } cancel() { return Promise.resolve(); } });
  // postMessage de la propia ventana (los captchas y widgets lo usan entre si)
  if (typeof G.postMessage !== 'function') G.postMessage = (d, o) => { later(() => { const e = new Event('message'); e.data = d; e.origin = (G.location && G.location.origin) || ''; e.source = G; try { G.dispatchEvent(e); } catch (x) {} if (typeof G.onmessage === 'function') { try { G.onmessage(e); } catch (x) {} } }); };
  def('XMLSerializer', class XMLSerializer { serializeToString(n) { return n && n.outerHTML !== undefined ? n.outerHTML : (n && n.textContent) || ''; } });
  def('visualViewport', Object.assign(new EventTarget(), { width: 1920, height: 1080, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0, scale: 1 }));
  // Intl basico: no hay Intl en QuickJS; sin el, cualquier new Intl.NumberFormat() rompia el script
  if (typeof G.Intl === 'undefined') {
    const grp = (s) => s.replace(/\B(?=(\d{3})+(?!\d))/g, ',');
    const fmtNum = (n, o) => { o = o || {}; n = Number(n); if (!isFinite(n)) return String(n); const mx = o.maximumFractionDigits !== undefined ? o.maximumFractionDigits : (o.style === 'currency' ? 2 : 3); const mn = o.minimumFractionDigits !== undefined ? o.minimumFractionDigits : (o.style === 'currency' ? 2 : 0);
      let s = Math.abs(n).toFixed(Math.max(mx, mn)); if (s.indexOf('.') >= 0) { let [i, f] = s.split('.'); while (f.length > mn && f.endsWith('0')) f = f.slice(0, -1); s = f.length ? i + '.' + f : i; } const ip = s.split('.'); ip[0] = o.useGrouping === false ? ip[0] : grp(ip[0]); s = ip.join('.');
      if (o.style === 'percent') s = fmtNum(n * 100, { maximumFractionDigits: o.maximumFractionDigits || 0 }).replace('-', '') + '%'; if (o.style === 'currency') s = ({ USD: '$', EUR: '€', GBP: '£', JPY: '¥' }[o.currency] || (o.currency || '') + ' ') + s; return (n < 0 ? '-' : '') + s; };
    const pad = (n, l) => String(n).padStart(l || 2, '0');
    G.Intl = {
      NumberFormat: class NumberFormat { constructor(l, o) { this.o = o || {}; } format(n) { return fmtNum(n, this.o); } formatToParts(n) { return [{ type: 'literal', value: this.format(n) }]; } resolvedOptions() { return { locale: 'en-US', ...this.o }; } static supportedLocalesOf() { return ['en-US']; } },
      DateTimeFormat: class DateTimeFormat { constructor(l, o) { this.o = o || {}; } format(d) { d = d ? new Date(d) : new Date(); return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate()) + (this.o.hour || this.o.timeStyle ? ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes()) : ''); } formatToParts(d) { return [{ type: 'literal', value: this.format(d) }]; } resolvedOptions() { return { locale: 'en-US', timeZone: 'UTC', calendar: 'gregory', numberingSystem: 'latn' }; } static supportedLocalesOf() { return ['en-US']; } },
      PluralRules: class PluralRules { constructor(l, o) { this.o = o || {}; } select(n) { if (this.o.type === 'ordinal') { const a = n % 10, b = n % 100; return a === 1 && b !== 11 ? 'one' : a === 2 && b !== 12 ? 'two' : a === 3 && b !== 13 ? 'few' : 'other'; } return n === 1 ? 'one' : 'other'; } resolvedOptions() { return { locale: 'en-US' }; } },
      Collator: class Collator { constructor(l, o) { this.o = o || {}; } compare(a, b) { a = String(a); b = String(b); if (this.o.sensitivity === 'base' || this.o.sensitivity === 'accent') { a = a.toLowerCase(); b = b.toLowerCase(); } return a < b ? -1 : a > b ? 1 : 0; } resolvedOptions() { return { locale: 'en-US' }; } },
      RelativeTimeFormat: class RelativeTimeFormat { constructor(l, o) {} format(v, u) { u = String(u).replace(/s$/, ''); const n = Math.abs(v); const t = n + ' ' + u + (n === 1 ? '' : 's'); return v < 0 ? t + ' ago' : 'in ' + t; } },
      ListFormat: class ListFormat { constructor(l, o) { this.o = o || {}; } format(a) { a = Array.from(a); const w = this.o.type === 'disjunction' ? 'or' : 'and'; return a.length < 2 ? a.join('') : a.length === 2 ? a[0] + ' ' + w + ' ' + a[1] : a.slice(0, -1).join(', ') + ', ' + w + ' ' + a[a.length - 1]; } },
      getCanonicalLocales: l => (Array.isArray(l) ? l : l ? [l] : []).map(String),
      supportedValuesOf: () => []
    };
  }
})();
// ---- fin stubs v0.9.4


// ---------------------------------------------------------------- v0.9.6: NodeFilter/TreeWalker, Path2D, audio silencioso, pantalla completa
(function () {
  const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, enumerable: false, configurable: true });
  G.NodeFilter = { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 1, SHOW_ATTRIBUTE: 2, SHOW_TEXT: 4, SHOW_CDATA_SECTION: 8, SHOW_PROCESSING_INSTRUCTION: 64, SHOW_COMMENT: 128, SHOW_DOCUMENT: 256, SHOW_DOCUMENT_TYPE: 512, SHOW_DOCUMENT_FRAGMENT: 1024 };
  const kids = n => (n && n.childNodes) ? Array.from(n.childNodes) : [];
  const accept = (n, what, f) => {
    if (!(what & (1 << ((n.nodeType | 0) - 1)))) return 3;
    if (!f) return 1;
    const r = typeof f === 'function' ? f(n) : (f.acceptNode ? f.acceptNode(n) : 1);
    return r === 2 || r === 3 ? r : 1;
  };
  const nextAfter = (n, root, skipKids) => {
    if (!skipKids) { const k = kids(n); if (k.length) return k[0]; }
    while (n && n !== root) {
      const p = n.parentNode; if (!p) return null;
      const sib = kids(p), i = sib.indexOf(n);
      if (i >= 0 && i + 1 < sib.length) return sib[i + 1];
      n = p;
    }
    return null;
  };
  const prevOf = (n, root) => {
    if (!n || n === root) return null;
    const p = n.parentNode; if (!p) return null;
    const sib = kids(p), i = sib.indexOf(n);
    if (i > 0) { let m = sib[i - 1], k; while ((k = kids(m)).length) m = k[k.length - 1]; return m; }
    return p;
  };
  // Simplificado: filter se evalua al visitar; FILTER_REJECT salta el subarbol solo en nextNode.
  class TreeWalker {
    constructor(root, what, filter) { def(this, 'root', root); def(this, 'whatToShow', what === undefined ? 0xFFFFFFFF : what >>> 0); def(this, 'filter', filter || null); this.currentNode = root; }
    nextNode() { let n = this.currentNode, skip = false; for (;;) { n = nextAfter(n, this.root, skip); if (!n) return null; const r = accept(n, this.whatToShow, this.filter); if (r === 1) { this.currentNode = n; return n; } skip = r === 2; } }
    previousNode() { let n = this.currentNode; for (;;) { n = prevOf(n, this.root); if (!n) return null; if (accept(n, this.whatToShow, this.filter) === 1) { this.currentNode = n; return n; } } }
    parentNode() { let n = this.currentNode; while (n && n !== this.root) { n = n.parentNode; if (n && accept(n, this.whatToShow, this.filter) === 1) { this.currentNode = n; return n; } } return null; }
    firstChild() { for (const c of kids(this.currentNode)) if (accept(c, this.whatToShow, this.filter) === 1) { this.currentNode = c; return c; } return null; }
    lastChild() { const k = kids(this.currentNode); for (let i = k.length - 1; i >= 0; i--) if (accept(k[i], this.whatToShow, this.filter) === 1) { this.currentNode = k[i]; return k[i]; } return null; }
    nextSibling() { const n = this.currentNode, p = n && n.parentNode; if (!p || n === this.root) return null; const k = kids(p); for (let i = k.indexOf(n) + 1; i > 0 && i < k.length; i++) if (accept(k[i], this.whatToShow, this.filter) === 1) { this.currentNode = k[i]; return k[i]; } return null; }
    previousSibling() { const n = this.currentNode, p = n && n.parentNode; if (!p || n === this.root) return null; const k = kids(p); for (let i = k.indexOf(n) - 1; i >= 0; i--) if (accept(k[i], this.whatToShow, this.filter) === 1) { this.currentNode = k[i]; return k[i]; } return null; }
  }
  // NodeIterator: instantanea del arbol al primer uso (no es "viva").
  class NodeIterator {
    constructor(root, what, filter) { def(this, 'root', root); def(this, 'whatToShow', what === undefined ? 0xFFFFFFFF : what >>> 0); def(this, 'filter', filter || null); def(this, '__list', null); def(this, '__i', -1); this.referenceNode = root; this.pointerBeforeReferenceNode = true; }
    __build() { if (this.__list) return; const out = []; let n = this.root; for (;;) { const r = n === this.root ? accept(n, this.whatToShow, this.filter) : accept(n, this.whatToShow, this.filter); if (r === 1) out.push(n); n = nextAfter(n, this.root, r === 2); if (!n) break; } this.__list = out; }
    nextNode() { this.__build(); if (this.__i + 1 >= this.__list.length) return null; this.__i++; return (this.referenceNode = this.__list[this.__i]); }
    previousNode() { this.__build(); if (this.__i <= 0) return null; this.__i--; return (this.referenceNode = this.__list[this.__i]); }
    detach() {}
  }
  G.TreeWalker = TreeWalker; G.NodeIterator = NodeIterator;
  Document.prototype.createTreeWalker = function (root, what, filter) { return new TreeWalker(root, what, filter); };
  Document.prototype.createNodeIterator = function (root, what, filter) { return new NodeIterator(root, what, filter); };

  // Path2D: se graba y se repite al llamar a ctx.fill(path)/stroke(path)/clip(path) (ver bloque Canvas 2D).
  class Path2D {
    constructor(p) { def(this, '__ops', p instanceof Path2D ? p.__ops.slice() : []); }
    addPath(p) { if (p instanceof Path2D) this.__ops.push(...p.__ops); }
  }
  for (const m of ['moveTo', 'lineTo', 'quadraticCurveTo', 'bezierCurveTo', 'arc', 'arcTo', 'ellipse', 'rect', 'roundRect', 'closePath']) Path2D.prototype[m] = function (...a) { this.__ops.push([m, a]); };
  G.Path2D = Path2D;

  // Audio SIN SONIDO: las paginas/juegos que cargan sonidos no se quedan esperando ni fallan. No hay salida de audio para JS.
  const fire = (el, names) => { let d = 0; for (const n of names) G.setTimeout(() => { try { el.dispatchEvent(new G.Event(n)); const h = el['on' + n]; if (typeof h === 'function') h.call(el, new G.Event(n)); } catch (e) {} }, d += 1); };
  const E = Element.prototype;
  if (!('canPlayType' in E)) E.canPlayType = function () { return ''; };
  if (!('readyState' in E)) Object.defineProperty(E, 'readyState', { get() { return (this.localName === 'audio' || this.localName === 'video') ? 4 : undefined; }, configurable: true });
  if (!('ended' in E)) Object.defineProperty(E, 'ended', { get() { return false; }, configurable: true });
  if (!('playbackRate' in E)) Object.defineProperty(E, 'playbackRate', { get() { return 1; }, set(v) {}, configurable: true });
  const origLoad = E.load;
  E.load = function () { if (this.localName === 'audio') fire(this, ['loadstart', 'loadeddata', 'canplay', 'canplaythrough']); return origLoad ? origLoad.call(this) : undefined; };
  G.Audio = function (src) { const a = document.createElement('audio'); if (src) { a.setAttribute('src', src); fire(a, ['loadstart', 'loadeddata', 'canplay', 'canplaythrough']); } return a; };
  const fakeBuf = (n, sr) => ({ length: n | 0, sampleRate: sr || 44100, numberOfChannels: 2, duration: (n | 0) / (sr || 44100), getChannelData: () => new Float32Array(n | 0), copyFromChannel() {}, copyToChannel() {} });
  const param = (v) => ({ value: v === undefined ? 1 : v, defaultValue: v === undefined ? 1 : v, setValueAtTime() { return this; }, linearRampToValueAtTime() { return this; }, exponentialRampToValueAtTime() { return this; }, setTargetAtTime() { return this; }, cancelScheduledValues() { return this; } });
  const node = (extra) => Object.assign({ connect(x) { return x; }, disconnect() {}, addEventListener() {}, removeEventListener() {}, start() {}, stop() {} }, extra || {});
  class AudioContext {
    constructor() { this.state = 'running'; this.sampleRate = 44100; this.currentTime = 0; this.destination = node({ maxChannelCount: 2 }); this.listener = {}; this.onstatechange = null; }
    createGain() { return node({ gain: param(1) }); }
    createOscillator() { return node({ frequency: param(440), detune: param(0), type: 'sine', setPeriodicWave() {} }); }
    createBufferSource() { return node({ buffer: null, loop: false, playbackRate: param(1), detune: param(0), onended: null }); }
    createBuffer(c, n, sr) { return fakeBuf(n, sr); }
    createAnalyser() { return node({ fftSize: 2048, frequencyBinCount: 1024, getByteFrequencyData() {}, getByteTimeDomainData() {}, getFloatFrequencyData() {}, getFloatTimeDomainData() {} }); }
    createBiquadFilter() { return node({ type: 'lowpass', frequency: param(350), Q: param(1), gain: param(0) }); }
    createDynamicsCompressor() { return node({ threshold: param(-24), knee: param(30), ratio: param(12), attack: param(0.003), release: param(0.25) }); }
    createStereoPanner() { return node({ pan: param(0) }); }
    createPanner() { return node({ positionX: param(0), positionY: param(0), positionZ: param(0), setPosition() {} }); }
    createDelay() { return node({ delayTime: param(0) }); }
    createConvolver() { return node({ buffer: null }); }
    createScriptProcessor() { return node({ onaudioprocess: null }); }
    createMediaElementSource() { return node(); }
    decodeAudioData(data, ok, err) { const b = fakeBuf(44100, 44100); if (typeof ok === 'function') G.setTimeout(() => ok(b), 1); return Promise.resolve(b); }
    resume() { return Promise.resolve(); } suspend() { return Promise.resolve(); } close() { return Promise.resolve(); }
    addEventListener() {} removeEventListener() {}
  }
  G.AudioContext = AudioContext; G.webkitAudioContext = AudioContext; G.OfflineAudioContext = undefined;

  // Pantalla completa / orientacion / vibracion: no hacen nada, pero las paginas no fallan al llamarlas.
  if (!E.requestFullscreen) E.requestFullscreen = function () { return Promise.resolve(); };
  E.webkitRequestFullscreen = E.requestFullscreen; E.requestPointerLock = function () {}; 
  const Dp = Document.prototype;
  if (!('fullscreenEnabled' in Dp)) Object.defineProperty(Dp, 'fullscreenEnabled', { get() { return false; }, configurable: true });
  if (!('fullscreenElement' in Dp)) Object.defineProperty(Dp, 'fullscreenElement', { get() { return null; }, configurable: true });
  if (!Dp.exitFullscreen) Dp.exitFullscreen = function () { return Promise.resolve(); };
  if (!Dp.exitPointerLock) Dp.exitPointerLock = function () {};
  if (G.navigator && !G.navigator.vibrate) G.navigator.vibrate = () => false;
  if (G.screen && G.screen.orientation && !G.screen.orientation.lock) { G.screen.orientation.lock = () => Promise.resolve(); G.screen.orientation.unlock = () => {}; }
})();
// ---- fin v0.9.6

// ---- Canvas 2D (v0.9.5): getContext('2d') sobre el rasterizador nativo __c (app/canvas2d.cpp). Sin __c (PSB_NO_CANVAS) getContext sigue devolviendo null.
(function () {
  const C = G.__c;
  if (!C) return;
  const O = { FILLRECT: 1, STROKERECT: 2, CLEARRECT: 3, BEGINPATH: 4, CLOSEPATH: 5, MOVETO: 6, LINETO: 7, QUADTO: 8, BEZIERTO: 9, ARC: 10, RECT: 11, FILL: 12, STROKE: 13, SAVE: 14, RESTORE: 15, TRANSLATE: 16, SCALE: 17, ROTATE: 18, TRANSFORM: 19, SETTRANSFORM: 20, RESETTRANSFORM: 21, DRAWIMAGE: 22, LINEWIDTH: 23, ALPHA: 24, CLIP: 25, FILLTEXT: 26, STROKETEXT: 27, LINECAP: 28, LINEJOIN: 29, SMOOTH: 30, ELLIPSE: 31, MEASURETEXT: 32, GRAD_LINEAR: 33, GRAD_RADIAL: 34, GRAD_STOP: 35, FILLGRAD: 36, STROKEGRAD: 37, ARCTO: 38 };
  const S = { FILL: 0, STROKE: 1, FONT: 2, ALIGN: 3, BASELINE: 4, COMPOSITE: 5, TEXT: 6, STOPCOLOR: 7 };
  const def = (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, enumerable: false, configurable: true });
  const canvasEls = new Map();
  let anyCtx = false;
  class CanvasGradient { constructor(cv, gid) { def(this, '__cv', cv); def(this, '__gid', gid); } addColorStop(off, col) { C.str(this.__cv, S.STOPCOLOR, String(col)); C.op(this.__cv, O.GRAD_STOP, this.__gid, +off || 0); } }
  class CanvasPattern {}
  class ImageData {
    constructor(a, b, c) {
      if (typeof a === 'number') { this.width = a | 0; this.height = b | 0; this.data = new Uint8ClampedArray(this.width * this.height * 4); }
      else { this.data = a; this.width = b | 0; this.height = c === undefined ? ((a.length / 4 / (b | 0)) | 0) : c | 0; }
      this.colorSpace = 'srgb';
    }
  }
  const CAP = { butt: 0, round: 1, square: 2 }, JOIN = { miter: 0, round: 1, bevel: 2 };
  const srcOf = img => {
    if (!img) return null;
    if (img.__cid) return { id: img.__cid, w: img.width | 0, h: img.height | 0 };
    if (img.localName === 'img') return ensureImg(img);
    return null;
  };
  class Ctx2D {
    constructor(canvas, id) { def(this, 'canvas', canvas); def(this, '__id', id); this.__reset(); }
    __reset() {
      def(this, '__p', { fillStyle: '#000000', strokeStyle: '#000000', lineWidth: 1, globalAlpha: 1, font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic', globalCompositeOperation: 'source-over', imageSmoothingEnabled: true, lineCap: 'butt', lineJoin: 'miter' });
      def(this, '__stack', []);
    }
    save() { C.op(this.__id, O.SAVE); this.__stack.push(Object.assign({}, this.__p)); }
    restore() { C.op(this.__id, O.RESTORE); const s = this.__stack.pop(); if (s) def(this, '__p', s); }
    beginPath() { C.op(this.__id, O.BEGINPATH); } closePath() { C.op(this.__id, O.CLOSEPATH); }
    moveTo(x, y) { C.op(this.__id, O.MOVETO, x, y); } lineTo(x, y) { C.op(this.__id, O.LINETO, x, y); }
    quadraticCurveTo(a, b, c, d) { C.op(this.__id, O.QUADTO, a, b, c, d); } bezierCurveTo(a, b, c, d, e, f) { C.op(this.__id, O.BEZIERTO, a, b, c, d, e, f); }
    arc(x, y, r, a0, a1, ccw) { C.op(this.__id, O.ARC, x, y, r, a0, a1, ccw ? 1 : 0); }
    arcTo(a, b, c, d, r) { C.op(this.__id, O.ARCTO, a, b, c, d, r); }
    ellipse(x, y, rx, ry, rot, a0, a1, ccw) { C.op(this.__id, O.ELLIPSE, x, y, rx, ry, rot, a0, a1, ccw ? 1 : 0); }
    rect(x, y, w, h) { C.op(this.__id, O.RECT, x, y, w, h); }
    roundRect(x, y, w, h, r) {
      r = Array.isArray(r) ? +r[0] || 0 : +r || 0; r = Math.max(0, Math.min(r, Math.abs(w) / 2, Math.abs(h) / 2));
      const P = Math.PI; this.moveTo(x + r, y); this.lineTo(x + w - r, y); this.arc(x + w - r, y + r, r, -P / 2, 0); this.lineTo(x + w, y + h - r); this.arc(x + w - r, y + h - r, r, 0, P / 2);
      this.lineTo(x + r, y + h); this.arc(x + r, y + h - r, r, P / 2, P); this.lineTo(x, y + r); this.arc(x + r, y + r, r, P, 3 * P / 2); this.closePath();
    }
    __replay(p) { C.op(this.__id, O.BEGINPATH); for (const o of p.__ops) this[o[0]](...o[1]); }
    fill(a, b) { let rule = a; if (a instanceof G.Path2D) { this.__replay(a); rule = b; } C.op(this.__id, O.FILL, rule === 'evenodd' ? 1 : 0); }
    stroke(p) { if (p instanceof G.Path2D) this.__replay(p); C.op(this.__id, O.STROKE); }
    clip(a, b) { if (a instanceof G.Path2D) this.__replay(a); C.op(this.__id, O.CLIP); }
    fillRect(x, y, w, h) { C.op(this.__id, O.FILLRECT, x, y, w, h); } strokeRect(x, y, w, h) { C.op(this.__id, O.STROKERECT, x, y, w, h); } clearRect(x, y, w, h) { C.op(this.__id, O.CLEARRECT, x, y, w, h); }
    translate(x, y) { C.op(this.__id, O.TRANSLATE, x, y); } scale(x, y) { C.op(this.__id, O.SCALE, x, y); } rotate(a) { C.op(this.__id, O.ROTATE, a); }
    transform(a, b, c, d, e, f) { C.op(this.__id, O.TRANSFORM, a, b, c, d, e, f); } setTransform(a, b, c, d, e, f) { if (a && typeof a === 'object') { C.op(this.__id, O.SETTRANSFORM, a.a, a.b, a.c, a.d, a.e, a.f); } else if (arguments.length === 0) C.op(this.__id, O.RESETTRANSFORM); else C.op(this.__id, O.SETTRANSFORM, a, b, c, d, e, f); }
    resetTransform() { C.op(this.__id, O.RESETTRANSFORM); }
    getTransform() { return { a: 1, b: 0, c: 0, d: 1, e: 0, f: 0 }; }
    fillText(t, x, y, mw) { C.str(this.__id, S.TEXT, String(t)); C.op(this.__id, O.FILLTEXT, x, y, mw || 0); }
    strokeText(t, x, y, mw) { C.str(this.__id, S.TEXT, String(t)); C.op(this.__id, O.STROKETEXT, x, y, mw || 0); }
    measureText(t) { C.str(this.__id, S.TEXT, String(t)); const w = C.op(this.__id, O.MEASURETEXT); return { width: w, actualBoundingBoxLeft: 0, actualBoundingBoxRight: w, actualBoundingBoxAscent: 0, actualBoundingBoxDescent: 0, fontBoundingBoxAscent: 0, fontBoundingBoxDescent: 0 }; }
    drawImage(img, a, b, c, d, e, f, g, h) {
      const s = srcOf(img);
      if (!s || !s.id) return;
      const n = arguments.length;
      if (n >= 9) C.op(this.__id, O.DRAWIMAGE, s.id, a, b, c, d, e, f, g, h);
      else if (n >= 5) C.op(this.__id, O.DRAWIMAGE, s.id, 0, 0, s.w, s.h, a, b, c, d);
      else C.op(this.__id, O.DRAWIMAGE, s.id, 0, 0, s.w, s.h, a, b, s.w, s.h);
    }
    createLinearGradient(x0, y0, x1, y1) { return new CanvasGradient(this.__id, C.op(this.__id, O.GRAD_LINEAR, x0, y0, x1, y1)); }
    createRadialGradient(x0, y0, r0, x1, y1, r1) { return new CanvasGradient(this.__id, C.op(this.__id, O.GRAD_RADIAL, x0, y0, r0, x1, y1, r1)); }
    createConicGradient() { return new CanvasGradient(this.__id, -1); }
    createPattern() { return new CanvasPattern(); }
    getImageData(x, y, w, h) { w |= 0; h |= 0; const d = new ImageData(w, h); const b = C.getPixels(this.__id, x | 0, y | 0, w, h); if (b) d.data.set(new Uint8ClampedArray(b)); return d; }
    putImageData(img, x, y) { if (!img || !img.data) return; const u = img.data; const buf = (u.byteOffset === 0 && u.byteLength === u.buffer.byteLength) ? u.buffer : u.slice().buffer; C.putPixels(this.__id, x | 0, y | 0, img.width | 0, img.height | 0, buf); }
    createImageData(w, h) { return (w && typeof w === 'object') ? new ImageData(w.width, w.height) : new ImageData(w, h); }
    setLineDash() {} getLineDash() { return []; } isPointInPath() { return false; } isPointInStroke() { return false; } drawFocusIfNeeded() {} getContextAttributes() { return { alpha: true }; }
  }
  const P = (name, send) => Object.defineProperty(Ctx2D.prototype, name, { get() { return this.__p[name]; }, set(v) { if (send.call(this, v) !== false) this.__p[name] = v; }, enumerable: true, configurable: true });
  P('fillStyle', function (v) { if (v instanceof CanvasGradient) { C.op(this.__id, O.FILLGRAD, v.__gid); return true; } if (typeof v === 'string') { C.str(this.__id, S.FILL, v); return true; } return false; });
  P('strokeStyle', function (v) { if (v instanceof CanvasGradient) { C.op(this.__id, O.STROKEGRAD, v.__gid); return true; } if (typeof v === 'string') { C.str(this.__id, S.STROKE, v); return true; } return false; });
  P('lineWidth', function (v) { v = +v; if (!(v > 0) || !isFinite(v)) return false; C.op(this.__id, O.LINEWIDTH, v); });
  P('globalAlpha', function (v) { v = +v; if (!(v >= 0 && v <= 1)) return false; C.op(this.__id, O.ALPHA, v); });
  P('font', function (v) { C.str(this.__id, S.FONT, String(v)); });
  P('textAlign', function (v) { C.str(this.__id, S.ALIGN, String(v)); });
  P('textBaseline', function (v) { C.str(this.__id, S.BASELINE, String(v)); });
  P('globalCompositeOperation', function (v) { C.str(this.__id, S.COMPOSITE, String(v)); });
  P('imageSmoothingEnabled', function (v) { C.op(this.__id, O.SMOOTH, v ? 1 : 0); });
  P('lineCap', function (v) { if (!(v in CAP)) return false; C.op(this.__id, O.LINECAP, CAP[v]); });
  P('lineJoin', function (v) { if (!(v in JOIN)) return false; C.op(this.__id, O.LINEJOIN, JOIN[v]); });
  for (const k of ['shadowBlur', 'shadowOffsetX', 'shadowOffsetY', 'lineDashOffset', 'miterLimit']) Object.defineProperty(Ctx2D.prototype, k, { get() { return this['__' + k] === undefined ? (k === 'miterLimit' ? 10 : 0) : this['__' + k]; }, set(v) { def(this, '__' + k, v); }, configurable: true });
  for (const k of ['shadowColor', 'filter', 'imageSmoothingQuality', 'direction', 'fontKerning']) Object.defineProperty(Ctx2D.prototype, k, { get() { return this['__' + k] === undefined ? (k === 'shadowColor' ? 'rgba(0, 0, 0, 0)' : k === 'filter' ? 'none' : k === 'direction' ? 'inherit' : 'low') : this['__' + k]; }, set(v) { def(this, '__' + k, v); }, configurable: true });

  // ---- <canvas>
  const dimOf = (el, k) => { const v = parseInt(el.getAttribute(k), 10); return v >= 0 ? v : (k === 'width' ? 300 : 150); };
  Element.prototype.getContext = function (type) {
    if (this.localName !== 'canvas' || String(type) !== '2d') return null;
    if (!this.__ctx2d) {
      const id = C.create(dimOf(this, 'width'), dimOf(this, 'height'));
      if (!id) return null;
      def(this, '__cid', id); def(this, '__ctx2d', new Ctx2D(this, id)); canvasEls.set(id, this); anyCtx = true;
    }
    return this.__ctx2d;
  };
  for (const k of ['width', 'height']) {
    const d = Object.getOwnPropertyDescriptor(Element.prototype, k) || { get() { return this.getAttribute(k) || ''; }, set(v) { this.setAttribute(k, v); } };
    Object.defineProperty(Element.prototype, k, {
      configurable: true,
      get() {
        if (this.localName === 'canvas') return dimOf(this, k);
        if (this.localName === 'img') { const a = parseInt(this.getAttribute(k), 10); return a >= 0 ? a : (k === 'width' ? this.naturalWidth : this.naturalHeight); }
        return d.get.call(this);
      },
      set(v) {
        d.set.call(this, v);
        if (this.localName === 'canvas' && this.__cid) { C.resize(this.__cid, dimOf(this, 'width'), dimOf(this, 'height')); this.__ctx2d.__reset(); }
      }
    });
  }
  const gbr = Element.prototype.getBoundingClientRect;
  Element.prototype.getBoundingClientRect = function () { if (this.localName === 'canvas') { const w = dimOf(this, 'width'), h = dimOf(this, 'height'); return { x: 0, y: 0, top: 0, left: 0, right: w, bottom: h, width: w, height: h }; } return gbr.call(this); };
  for (const k of ['offsetWidth', 'clientWidth', 'offsetHeight', 'clientHeight']) {
    const d = Object.getOwnPropertyDescriptor(Element.prototype, k);
    Object.defineProperty(Element.prototype, k, { configurable: true, get() { return this.localName === 'canvas' ? dimOf(this, /Width/.test(k) ? 'width' : 'height') : (d ? d.get.call(this) : 0); } });
  }
  G.OffscreenCanvas = function (w, h) { const el = document.createElement('canvas'); el.setAttribute('width', w | 0); el.setAttribute('height', h | 0); return el; };
  G.createImageBitmap = img => Promise.resolve(img);
  G.CanvasRenderingContext2D = Ctx2D; G.CanvasGradient = CanvasGradient; G.CanvasPattern = CanvasPattern; G.ImageData = ImageData;

  // ---- imagenes para drawImage (descarga y decodificacion nativas, sincronas con presupuesto)
  function ensureImg(el) {
    if (el.__iid !== undefined) return el.__iid ? { id: el.__iid, w: el.__iw, h: el.__ih } : null;
    const src = el.getAttribute('src');
    if (!src) return null;
    const r = C.image(src);
    def(el, '__iid', r ? r.id : 0); def(el, '__iw', r ? r.w : 0); def(el, '__ih', r ? r.h : 0);
    return r ? { id: r.id, w: r.w, h: r.h } : null;
  }
  const sd = Object.getOwnPropertyDescriptor(Element.prototype, 'src');
  Object.defineProperty(Element.prototype, 'src', {
    configurable: true,
    get() { return sd.get.call(this); },
    set(v) {
      sd.set.call(this, v);
      if (this.localName !== 'img') return;
      def(this, '__iid', undefined);
      // Se carga ya si la pagina espera el evento load o ya usa canvas; si no (pixeles de seguimiento) no se gasta red.
      const wants = this.onload || this.onerror || (this.__l && (this.__l.load || this.__l.error)) || anyCtx;
      if (!wants || !this.getAttribute('src')) return;
      const ok = !!ensureImg(this);
      setTimeout(() => { this.dispatchEvent(new Event(ok ? 'load' : 'error')); }, 0);
    }
  });
  for (const k of ['naturalWidth', 'naturalHeight']) Object.defineProperty(Element.prototype, k, { configurable: true, get() { if (this.localName !== 'img') return 0; const s = this.__iid !== undefined ? (this.__iid ? { w: this.__iw, h: this.__ih } : null) : ensureImg(this); return s ? (k === 'naturalWidth' ? s.w : s.h) : 0; } });
  Element.prototype.decode = function () { return Promise.resolve(); };

  // ---- entrada desde el modo juego (la llama el nativo con jsx_eval)
  G.__psbKey = (down, key, code, kc) => {
    const o = { bubbles: true, cancelable: true, key, code, keyCode: kc };
    const ev = new KeyboardEvent(down ? 'keydown' : 'keyup', o);
    ev.which = kc; ev.charCode = 0; ev.repeat = false; ev.shiftKey = ev.ctrlKey = ev.altKey = ev.metaKey = false;
    (document.body || document).dispatchEvent(ev);
    if (down && key.length === 1) { const p = new KeyboardEvent('keypress', o); p.which = p.charCode = key.charCodeAt(0); (document.body || document).dispatchEvent(p); }
  };
  G.__psbMouse = (type, x, y, btn, cid) => {
    const el = canvasEls.get(cid) || document.body || document;
    const mk = (t, Cls) => { const ev = new Cls(t, { bubbles: true, cancelable: true, clientX: x, clientY: y, button: btn }); Object.assign(ev, { offsetX: x, offsetY: y, pageX: x, pageY: y, x, y, screenX: x, screenY: y, which: btn + 1, buttons: type === 'mousedown' ? 1 << btn : 0, pointerId: 1, pointerType: 'mouse', isPrimary: true, movementX: 0, movementY: 0 }); return ev; };
    if (type !== 'click') el.dispatchEvent(mk(type.replace('mouse', 'pointer'), G.PointerEvent || MouseEvent));
    el.dispatchEvent(mk(type, MouseEvent));
  };
  let pad = null;
  G.__psbPad = (axes, buttons) => {
    const first = !pad;
    pad = { id: 'Standard Gamepad (PS4 DualShock 4)', index: 0, connected: true, mapping: 'standard', timestamp: N.now(), axes: axes.slice(), buttons: buttons.map(v => ({ pressed: v > 0.5, touched: v > 0, value: v })), vibrationActuator: null, hapticActuators: [] };
    if (first) { const e = new Event('gamepadconnected'); e.gamepad = pad; try { G.dispatchEvent(e); } catch (x) {} }
  };
  G.__psbPadOff = () => { if (pad) { const e = new Event('gamepaddisconnected'); e.gamepad = pad; pad.connected = false; try { G.dispatchEvent(e); } catch (x) {} pad = null; } };
  if (G.navigator) G.navigator.getGamepads = () => [pad, null, null, null];
})();

// eventos de ciclo de vida (los llama el nativo)
G.__fire = (target, type) => { const ev = new Event(type, { bubbles: false }); (target === 'window' ? G : document).dispatchEvent(ev); };
G.__setReady = s => { document.__ready = s; document.dispatchEvent(new Event('readystatechange')); };
G.__click = id => { const el = wrap(id); if (el) el.click(); };
})(globalThis);
)JS";
