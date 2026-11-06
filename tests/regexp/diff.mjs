#!/usr/bin/env node
// Differential test of the regular expression engine (src/regexp) against V8: runs a hand-written
// corpus covering every feature and seeded random patterns and inputs through Node.js and through
// regexp_cli (tests/regexp/regexp_cli.cpp), and reports every difference in the syntax error message or
// in the match (index and all capture positions).
//
//   node tests/regexp/diff.mjs --cli build/tests/regexp_cli [--seed N] [--count N] [--show N] [--corpus-only]
//
// Two ES2025 features that the engine leaves out as Node.js 22 does (duplicate named groups, pattern
// modifiers (?i:...)) are accepted by newer Node.js: for them the engine's Node 22 error is expected.
import { spawnSync } from 'node:child_process';
import { CORPUS } from './corpus.mjs';

const args = process.argv.slice(2);
const opt = (name, def) => {
  const i = args.indexOf(name);
  return i >= 0 ? args[i + 1] : def;
};
const cli = opt('--cli');
if (!cli) {
  console.error('usage: diff.mjs --cli path/to/regexp_cli [--seed N] [--count N] [--show N] [--corpus-only]');
  process.exit(2);
}
const seed = Number(opt('--seed', '1'));
const count = args.includes('--corpus-only') ? 0 : Number(opt('--count', '5000'));
const show = Number(opt('--show', '30'));

// ---------------------------------------------------------------- random patterns
function rng(s) {
  // mulberry32
  return () => {
    s |= 0; s = (s + 0x6d2b79f5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const INPUT_CHARS = ['a', 'a', 'a', 'b', 'b', 'c', 'A', 'B', '1', '0', '_', ' ', '-', '\n', '\r', '\u2028', 'é', 'É',
  '\u017f', 's', 'S', 'k', 'K', '\u212a', '\u00df', '\u1e9e', '\u03c3', '\u03a3', '\u03c2', '\u{1F600}', '\u{1F601}',
  '\ud83d', '\ude00', '\u{10400}', '\u{10428}', 'x', 'y', '.', '(', '$', '\t', '\u0130', 'i', 'I', '\u0131'];

function generator(random) {
  const pick = (a) => a[Math.floor(random() * a.length)];
  const chance = (p) => random() < p;
  const int = (n) => Math.floor(random() * n);

  function makeCase() {
    const u = chance(0.4);
    let flags = '';
    for (const f of 'dgimsy') if (chance(f === 'i' ? 0.35 : f === 'm' || f === 's' ? 0.2 : 0.08)) flags += f;
    if (u) flags = flags.slice(0, flags.length - (flags.endsWith('y') ? 1 : 0)) + 'u' + (flags.endsWith('y') ? 'y' : '');
    const state = { groups: 0, names: [], u, depth: 0 };
    let pattern = disjunction(state);
    // forward references to groups that exist only after: \N up to the final count
    if (chance(0.05)) pattern = mutate(pattern);
    const inputs = [];
    for (let k = 0; k < 3; k++) {
      let s = '';
      const n = int(10);
      for (let i = 0; i < n; i++) s += pick(INPUT_CHARS);
      inputs.push(s);
    }
    return { pattern, flags, inputs };
  }

  function mutate(p) {
    const junk = ['(', ')', '[', ']', '{', '}', '*', '+', '?', '|', '\\', '^', '$', '-', '{1', '{2,1}', '(?', '(?<', '\\k', '\\p', '\\u{', '\\c', '\\x', '\\0', '\\8', '\\-', ']'];
    const at = int(p.length + 1);
    return p.slice(0, at) + pick(junk) + p.slice(at);
  }

  function disjunction(st) {
    const n = chance(0.25) ? 2 + int(2) : 1;
    const alts = [];
    for (let i = 0; i < n; i++) alts.push(alternative(st));
    return alts.join('|');
  }

  function alternative(st) {
    const n = int(4) + (st.depth === 0 ? 1 : 0);
    let s = '';
    for (let i = 0; i < n; i++) s += term(st);
    return s;
  }

  function term(st) {
    const r = random();
    if (r < 0.06) return pick(['^', '$', '\\b', '\\B']);
    if (r < 0.14 && st.depth < 3) return lookaround(st);
    let a = atom(st);
    if (a.quantifiable && chance(0.35)) a.text += quantifier();
    return a.text;
  }

  function quantifier() {
    let q = pick(['*', '+', '?', '{2}', '{0,1}', '{1,}', '{0,2}', '{1,3}', '{2,}', '{0}', '{3}']);
    if (chance(0.3)) q += '?';
    return q;
  }

  function lookaround(st) {
    st.depth++;
    const kind = pick(['?=', '?!', '?<=', '?<!']);
    const body = disjunction(st);
    st.depth--;
    let t = `(${kind}${body})`;
    // Annex B: quantified lookaheads without u
    if (!st.u && kind.length === 2 && chance(0.15)) t += quantifier();
    return t;
  }

  function atom(st) {
    const r = random();
    if (r < 0.35) return { text: literal(st), quantifiable: true };
    if (r < 0.42) return { text: '.', quantifiable: true };
    if (r < 0.52) return { text: charClass(st), quantifiable: true };
    if (r < 0.58) return { text: pick(['\\d', '\\D', '\\w', '\\W', '\\s', '\\S']), quantifiable: true };
    if (r < 0.62 && st.u) return { text: property(), quantifiable: true };
    if (r < 0.68 && st.groups > 0 || r < 0.645) {
      // backreference, possibly forward (to a group opened later) or to an enclosing group
      if (st.names.length && chance(0.4)) return { text: `\\k<${pick(st.names)}>`, quantifiable: true };
      return { text: `\\${1 + int(Math.max(1, st.groups + 1))}`, quantifiable: true };
    }
    if (st.depth < 3) {
      st.depth++;
      const kind = random();
      let open;
      if (kind < 0.5) { open = '('; st.groups++; }
      else if (kind < 0.7) {
        st.groups++;
        const name = pick(['a', 'b', 'c', 'x1', '$', '_n', 'π', 'ab']);
        if (st.names.includes(name)) { open = '('; } else { st.names.push(name); open = `(?<${name}>`; }
      } else open = '(?:';
      const body = disjunction(st);
      st.depth--;
      return { text: `${open}${body})`, quantifiable: true };
    }
    return { text: literal(st), quantifiable: true };
  }

  function escapeChar(c) {
    return '\\^$.*+?()[]{}|/'.includes(c) ? '\\' + c : c;
  }

  function literal(st) {
    const r = random();
    if (r < 0.6) return escapeChar(pick(['a', 'a', 'b', 'c', 'A', 'k', 's', 'S', 'é', 'É', 'x', '1', '-', ' ', '_', '.', '(', '$']));
    if (r < 0.7) return pick(['\u017f', '\u212a', '\u00df', '\u1e9e', '\u03c3', '\u03a3', '\u03c2', '\u0130', 'i', 'I', '\u0131']);
    if (r < 0.78) return pick(['\u{1F600}', '\u{10400}', '\u{10428}']);
    if (r < 0.85) return pick(['\\n', '\\t', '\\x61', '\\u0041', '\\u00e9', '\\0', '\\cJ', '\\f', '\\v', '\\r']);
    if (r < 0.9) return st.u ? pick(['\\u{1F600}', '\\u{61}', '\\ud83d\\ude00', '\\ud83d', '\\ude00', '\\u{10428}', '\\/'])
                             : pick(['\\ud83d', '\\ude00', '\\01', '\\8', '\\c', '\\q', '\\k', '\\p', ']', '{', '}', '\\u{2}']);
    return escapeChar(pick(['b', 'c', 'y', 'B']));
  }

  function classAtom(st) {
    const r = random();
    if (r < 0.5) return pick(['a', 'b', 'c', 'A', 'z', '0', '9', 'k', 's', 'é', '_', ' ', '.', '$', '(', '^', 'x', 'S', 'K']);
    if (r < 0.65) return pick(['\\d', '\\w', '\\s', '\\W', '\\D', '\\S']);
    if (r < 0.75) return pick(['\\b', '\\-', '\\n', '\\x41', '\\u00e9', '\\]', '\\\\', '\\0']);
    if (r < 0.8 && st.u) return property();
    if (r < 0.85) return pick(['\u017f', '\u212a', '\u00df', '\u03c3', '\u0130']);
    if (r < 0.9) return pick(['\u{1F600}', '\u{10400}', '\\ud83d']);
    if (!st.u) return pick(['\\c1', '\\c_', '\\8', '\\k', '\\B', '\\01']);
    return '\\u{1F601}';
  }

  function charClass(st) {
    let s = chance(0.3) ? '[^' : '[';
    const n = int(4);
    for (let i = 0; i < n; i++) {
      const a = classAtom(st);
      if (chance(0.3)) s += a + '-' + classAtom(st);
      else s += a;
    }
    return s + ']';
  }

  function property() {
    const p = pick(['L', 'Lu', 'Ll', 'N', 'Nd', 'P', 'Sm', 'Zs', 'Cc', 'Any', 'ASCII', 'Assigned', 'Alphabetic',
      'White_Space', 'Script=Greek', 'sc=Latn', 'scx=Grek', 'Script_Extensions=Latin', 'gc=Lt', 'General_Category=Letter',
      'Emoji', 'ID_Start', 'Lowercase', 'Uppercase', 'Cased', 'CWCF', 'Ideo', 'Hex', 'Math', 'Dash', 'Foo', 'L=x', 'sc=Foo']);
    return (chance(0.3) ? '\\P{' : '\\p{') + p + '}';
  }

  // Unicode data: one character (or a range, or a property) against characters that have case mappings
  function unicodeCase() {
    const u = chance(0.5);
    const esc = (c) => (u ? `\\u{${c.toString(16)}}` : c > 0xffff ? String.fromCodePoint(c) : `\\u${c.toString(16).padStart(4, '0')}`);
    const a = pick(CASED), r = random();
    let pattern;
    if (r < 0.4) pattern = esc(a);
    else if (r < 0.7) {
      const b = Math.min(a + int(300), u ? 0x10ffff : 0xffff);
      pattern = `[${chance(0.3) ? '^' : ''}${esc(a)}-${esc(b)}]`;
    } else if (r < 0.8) pattern = `(${esc(a)})\\1`;
    else if (u) pattern = (chance(0.3) ? '[^' : '[') + property() + ']';
    else pattern = pick(['\\w', '\\W', '[\\w-]', '\\b']);
    const flags = (chance(0.85) ? 'i' : '') + (u ? 'u' : '');
    const inputs = [];
    for (let k = 0; k < 3; k++) {
      const c = chance(0.8) ? pick(CASED) : int(0x110000);
      inputs.push(String.fromCodePoint(a) + String.fromCodePoint(c));
    }
    return { pattern: pattern.startsWith('(') ? pattern : `^.(?:${pattern})$`.replace('^.', u ? '^.' : '^[^]'), flags, inputs };
  }

  return () => (random() < 0.15 ? unicodeCase() : makeCase());
}

// code points with case mappings (and what they map to): the inputs of the case-folding cases
const CASED = (() => {
  const set = new Set();
  for (let c = 0; c <= 0x1ffff; c++) {
    if (c >= 0xd800 && c <= 0xdfff) continue;
    const s = String.fromCodePoint(c);
    const l = s.toLowerCase(), u = s.toUpperCase();
    if (l !== s || u !== s) {
      set.add(c);
      for (const t of [l, u]) if ([...t].length === 1) set.add(t.codePointAt(0));
    }
  }
  return [...set];
})();

// ---------------------------------------------------------------- running
const hex = (s) => { let h = ''; for (let i = 0; i < s.length; i++) h += s.charCodeAt(i).toString(16).padStart(4, '0'); return h; };

function v8Result(pattern, flags, input, start, sticky) {
  let re;
  try {
    re = new RegExp(pattern, flags);
  } catch (e) {
    return 'E\t' + e.message.toWellFormed();  // the engine's messages are UTF-8: lone surrogates become U+FFFD
  }
  const f = [...new Set(flags.replace(/[gy]/g, '') + 'd' + (sticky ? 'y' : 'g'))].join('');
  re = new RegExp(pattern, f);
  re.lastIndex = start;
  const m = re.exec(input);
  if (!m) return 'N';
  const caps = [];
  for (const r of m.indices) caps.push(...(r ? r : [-1, -1]));
  return 'M\t' + caps.join(' ');
}

// ES2025 features of newer Node.js versions that Node.js 22 (and the engine) reject: there the engine's
// Node 22 error is expected unless it gives the same result as V8 (an error that comes first)
function newerSyntax(pattern) {
  if (/\(\?[a-z]*-?[a-z]*[:)]/.test(pattern.replace(/\\./g, ''))) return 'Invalid group';
  const names = [...pattern.matchAll(/\(\?<([^=!>][^>]*)>/g)].map((m) => m[1]);
  if (new Set(names).size !== names.length) return 'Duplicate capture group name';
  return null;
}

const cases = [];
for (const [pattern, flags, inputs, starts] of CORPUS) {
  for (const input of inputs?.length ? inputs : ['']) for (const start of starts ?? [0]) cases.push({ pattern, flags, input, start, src: 'corpus' });
}
const make = generator(rng(seed));
for (let i = 0; i < count; i++) {
  const c = make();
  for (const input of c.inputs) {
    const start = input.length && i % 5 === 0 ? (i * 7) % (input.length + 1) : 0;
    cases.push({ pattern: c.pattern, flags: c.flags, input, start, src: 'random' });
  }
}

const version = spawnSync(cli, [], { input: '?version\n', encoding: 'utf8' }).stdout.trim();
const sameUnicode = version === process.versions.unicode;
if (!sameUnicode)
  console.log(`note: the engine's tables are Unicode ${version}, this Node.js has ${process.versions.unicode}: ` +
    'cases that depend on Unicode data (\\p, i with non-ASCII) are skipped');
const dependsOnUnicode = (c) => /\\[pP]/.test(c.pattern) || (c.flags.includes('i') && /[^\x00-\x7f]/.test(c.pattern + c.input));

const run = cases.filter((c) => sameUnicode || !dependsOnUnicode(c));
const input = run.map((c) => [c.flags, hex(c.pattern), hex(c.input), c.start, c.flags.includes('y') ? 1 : 0].join('\t')).join('\n') + '\n';
const out = spawnSync(cli, [], { input, encoding: 'utf8', maxBuffer: 1 << 30 });
if (out.status !== 0) {
  console.error(`regexp_cli failed (status ${out.status}, signal ${out.signal})\n${out.stderr}`);
  process.exit(1);
}
const lines = out.stdout.split('\n');
let mismatches = 0, budget = 0, known = 0, errors = 0, matches = 0;
for (let i = 0; i < run.length; i++) {
  const c = run[i];
  let ours = lines[i];
  if (ours === 'B') {
    if (++budget <= 3) console.log(`over budget: ${JSON.stringify(c.pattern)} flags=${c.flags} input=${JSON.stringify(c.input)}`);
    continue;
  }
  if (ours.startsWith('M')) ours = ours.split('\t').slice(0, 2).join('\t');
  let theirs = v8Result(c.pattern, c.flags, c.input, c.start, c.flags.includes('y'));
  const newer = newerSyntax(c.pattern);
  if (newer && ours !== theirs) {
    known++;
    if (ours.startsWith('E') && ours.endsWith(': ' + newer)) continue;
    theirs = `E\t... ${newer} (Node.js 22)`;
  }
  if (theirs.startsWith('E')) errors++;
  else if (theirs.startsWith('M')) matches++;
  if (ours !== theirs) {
    if (++mismatches <= show)
      console.log(`MISMATCH (${c.src}) ${JSON.stringify(c.pattern)} flags=${c.flags} input=${JSON.stringify(c.input)} start=${c.start}\n  v8:   ${theirs}\n  ours: ${ours}`);
  }
}
console.log(`${run.length} cases (${cases.length - run.length} skipped; corpus ${cases.filter((c) => c.src === 'corpus').length}, ` +
  `random ${count} patterns, seed ${seed}): ${matches} matches, ${errors} syntax errors, ${known} Node 22 syntax, ` +
  `${budget} over budget, ${mismatches} mismatches`);
process.exit(mismatches ? 1 : 0);
