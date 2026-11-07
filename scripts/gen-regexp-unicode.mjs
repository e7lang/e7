#!/usr/bin/env node
// Generates src/regexp/unicode_tables.inc: the Unicode data of the regular expression engine
// (docs/DESIGN.md, D4), taken from the Node.js that runs this script, so that \p{...} and
// case-insensitive matching give the same results as V8 (whose ICU this queries through RegExp).
//
//   node scripts/gen-regexp-unicode.mjs [--check]
//
// Property sets come from scanning every code point with /\p{name}+/gu; the case-insensitive
// equivalence classes from toUpperCase / toLowerCase candidates verified with /x/i and /x/iu.
// Names that this Node.js rejects (properties or scripts newer than its Unicode version) are left out;
// --check fails if the file on disk differs from what this Node.js generates.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const outFile = path.join(root, 'src/regexp/unicode_tables.inc');

// ---------------------------------------------------------------- property names (ES2024 tables 66-68)
// General_Category values with their aliases (PropertyValueAliases.txt); leaves first, then groups.
const GC_LEAVES = [
  ['Cc', 'Control', 'cntrl'], ['Cf', 'Format'], ['Cn', 'Unassigned'], ['Co', 'Private_Use'], ['Cs', 'Surrogate'],
  ['Ll', 'Lowercase_Letter'], ['Lm', 'Modifier_Letter'], ['Lo', 'Other_Letter'], ['Lt', 'Titlecase_Letter'],
  ['Lu', 'Uppercase_Letter'], ['Mc', 'Spacing_Mark'], ['Me', 'Enclosing_Mark'], ['Mn', 'Nonspacing_Mark'],
  ['Nd', 'Decimal_Number', 'digit'], ['Nl', 'Letter_Number'], ['No', 'Other_Number'],
  ['Pc', 'Connector_Punctuation'], ['Pd', 'Dash_Punctuation'], ['Pe', 'Close_Punctuation'],
  ['Pf', 'Final_Punctuation'], ['Pi', 'Initial_Punctuation'], ['Po', 'Other_Punctuation'], ['Ps', 'Open_Punctuation'],
  ['Sc', 'Currency_Symbol'], ['Sk', 'Modifier_Symbol'], ['Sm', 'Math_Symbol'], ['So', 'Other_Symbol'],
  ['Zl', 'Line_Separator'], ['Zp', 'Paragraph_Separator'], ['Zs', 'Space_Separator'],
];
const GC_GROUPS = [
  [['C', 'Other'], ['Cc', 'Cf', 'Cn', 'Co', 'Cs']],
  [['L', 'Letter'], ['Ll', 'Lm', 'Lo', 'Lt', 'Lu']],
  [['LC', 'Cased_Letter'], ['Ll', 'Lt', 'Lu']],
  [['M', 'Mark', 'Combining_Mark'], ['Mc', 'Me', 'Mn']],
  [['N', 'Number'], ['Nd', 'Nl', 'No']],
  [['P', 'Punctuation', 'punct'], ['Pc', 'Pd', 'Pe', 'Pf', 'Pi', 'Po', 'Ps']],
  [['S', 'Symbol'], ['Sc', 'Sk', 'Sm', 'So']],
  [['Z', 'Separator'], ['Zl', 'Zp', 'Zs']],
];
// Script values (PropertyValueAliases.txt of Unicode 18; the ones this Node.js does not know are skipped)
const SCRIPTS = `Adlm,Adlam Aghb,Caucasian_Albanian Ahom Arab,Arabic Armi,Imperial_Aramaic Armn,Armenian Avst,Avestan
Bali,Balinese Bamu,Bamum Bass,Bassa_Vah Batk,Batak Beng,Bengali Berf,Beria_Erfe Bhks,Bhaiksuki Bopo,Bopomofo
Brah,Brahmi Brai,Braille Bugi,Buginese Buhd,Buhid Cakm,Chakma Cans,Canadian_Aboriginal Cari,Carian Cham
Cher,Cherokee Chrs,Chorasmian Copt,Coptic,Qaac Cpmn,Cypro_Minoan Cprt,Cypriot Cyrl,Cyrillic Deva,Devanagari
Diak,Dives_Akuru Dogr,Dogra Dsrt,Deseret Dupl,Duployan Egyp,Egyptian_Hieroglyphs Elba,Elbasan Elym,Elymaic
Ethi,Ethiopic Gara,Garay Geor,Georgian Glag,Glagolitic Gong,Gunjala_Gondi Gonm,Masaram_Gondi Goth,Gothic
Gran,Grantha Grek,Greek Gujr,Gujarati Gukh,Gurung_Khema Guru,Gurmukhi Hang,Hangul Hani,Han Hano,Hanunoo
Hatr,Hatran Hebr,Hebrew Hira,Hiragana Hluw,Anatolian_Hieroglyphs Hmng,Pahawh_Hmong Hmnp,Nyiakeng_Puachue_Hmong
Hrkt,Katakana_Or_Hiragana Hung,Old_Hungarian Ital,Old_Italic Java,Javanese Jurc,Jurchen Kali,Kayah_Li
Kana,Katakana Kawi Khar,Kharoshthi Khmr,Khmer Khoj,Khojki Kits,Khitan_Small_Script Knda,Kannada Krai,Kirat_Rai
Kthi,Kaithi Lana,Tai_Tham Laoo,Lao Latn,Latin Lepc,Lepcha Limb,Limbu Lina,Linear_A Linb,Linear_B Lisu
Lyci,Lycian Lydi,Lydian Mahj,Mahajani Maka,Makasar Mand,Mandaic Mani,Manichaean Marc,Marchen Medf,Medefaidrin
Mend,Mende_Kikakui Merc,Meroitic_Cursive Mero,Meroitic_Hieroglyphs Mlym,Malayalam Modi Mong,Mongolian Mroo,Mro
Mtei,Meetei_Mayek Mult,Multani Mymr,Myanmar Nagm,Nag_Mundari Nand,Nandinagari Narb,Old_North_Arabian
Nbat,Nabataean Newa Nkoo,Nko Nshu,Nushu Ogam,Ogham Olck,Ol_Chiki Onao,Ol_Onal Orkh,Old_Turkic Orya,Oriya
Osge,Osage Osma,Osmanya Ougr,Old_Uyghur Palm,Palmyrene Pauc,Pau_Cin_Hau Pcun,Proto_Cuneiform Perm,Old_Permic
Phag,Phags_Pa Phli,Inscriptional_Pahlavi Phlp,Psalter_Pahlavi Phnx,Phoenician Plrd,Miao Prti,Inscriptional_Parthian
Rjng,Rejang Rohg,Hanifi_Rohingya Runr,Runic Samr,Samaritan Sarb,Old_South_Arabian Saur,Saurashtra Seal
Sgnw,SignWriting Shaw,Shavian Shrd,Sharada Sidd,Siddham Sidt,Sidetic Sind,Khudawadi Sinh,Sinhala Sogd,Sogdian
Sogo,Old_Sogdian Sora,Sora_Sompeng Soyo,Soyombo Sund,Sundanese Sunu,Sunuwar Sylo,Syloti_Nagri Syrc,Syriac
Tagb,Tagbanwa Takr,Takri Tale,Tai_Le Talu,New_Tai_Lue Taml,Tamil Tang,Tangut Tavt,Tai_Viet Tayo,Tai_Yo
Telu,Telugu Tfng,Tifinagh Tglg,Tagalog Thaa,Thaana Thai Tibt,Tibetan Tirh,Tirhuta Tnsa,Tangsa Todr,Todhri
Tols,Tolong_Siki Toto Tutg,Tulu_Tigalari Ugar,Ugaritic Vaii,Vai Vith,Vithkuqi Wara,Warang_Citi Wcho,Wancho
Xpeo,Old_Persian Xsux,Cuneiform Yezi,Yezidi Yiii,Yi Zanb,Zanabazar_Square Zinh,Inherited,Qaai Zyyy,Common
Zzzz,Unknown`.split(/\s+/).map((s) => s.split(','));
// Binary properties (ES2024 table 67) plus candidate aliases; Node.js decides which spellings it accepts.
const BINARY = [
  ['ASCII_Hex_Digit', 'AHex'], ['Alphabetic', 'Alpha'], ['Bidi_Control', 'Bidi_C'], ['Bidi_Mirrored', 'Bidi_M'],
  ['Case_Ignorable', 'CI'], ['Cased'], ['Changes_When_Casefolded', 'CWCF'], ['Changes_When_Casemapped', 'CWCM'],
  ['Changes_When_Lowercased', 'CWL'], ['Changes_When_NFKC_Casefolded', 'CWKCF'], ['Changes_When_Titlecased', 'CWT'],
  ['Changes_When_Uppercased', 'CWU'], ['Dash'], ['Default_Ignorable_Code_Point', 'DI'], ['Deprecated', 'Dep'],
  ['Diacritic', 'Dia'], ['Emoji'], ['Emoji_Component', 'EComp'], ['Emoji_Modifier', 'EMod'],
  ['Emoji_Modifier_Base', 'EBase'], ['Emoji_Presentation', 'EPres'], ['Extended_Pictographic', 'ExtPict'],
  ['Extender', 'Ext'], ['Grapheme_Base', 'Gr_Base'], ['Grapheme_Extend', 'Gr_Ext'], ['Hex_Digit', 'Hex'],
  ['IDS_Binary_Operator', 'IDSB'], ['IDS_Trinary_Operator', 'IDST'], ['IDS_Unary_Operator', 'IDSU'],
  ['ID_Continue', 'IDC'], ['ID_Start', 'IDS'], ['Ideographic', 'Ideo'], ['Join_Control', 'Join_C'],
  ['Logical_Order_Exception', 'LOE'], ['Lowercase', 'Lower'], ['Math'], ['Noncharacter_Code_Point', 'NChar'],
  ['Pattern_Syntax', 'Pat_Syn'], ['Pattern_White_Space', 'Pat_WS'], ['Quotation_Mark', 'QMark'], ['Radical'],
  ['Regional_Indicator', 'RI'], ['Sentence_Terminal', 'STerm'], ['Soft_Dotted', 'SD'],
  ['Terminal_Punctuation', 'Term'], ['Unified_Ideograph', 'UIdeo'], ['Uppercase', 'Upper'],
  ['Variation_Selector', 'VS'], ['White_Space', 'space', 'WSpace'], ['XID_Continue', 'XIDC'], ['XID_Start', 'XIDS'],
  ['ID_Compat_Math_Start'], ['ID_Compat_Math_Continue'], ['Modifier_Combining_Mark', 'MCM'],
  ['Prepended_Concatenation_Mark', 'PCM'], ['Hyphen'], ['Full_Composition_Exclusion', 'Comp_Ex'],
];

// ---------------------------------------------------------------- scanning code points
// Every code point except the surrogates, in order, as one string; surrogates are tested one by one
// (adjacent lead and trail surrogates would pair up).
const parts = [];
for (let cp = 0; cp <= 0x10ffff; cp++) if (cp < 0xd800 || cp > 0xdfff) parts.push(String.fromCodePoint(cp));
const ALL = parts.join('');
const cpAt = (off) => (off < 0xd800 ? off : off < 0xf800 ? off + 0x800 : 0x10000 + ((off - 0xf800) >> 1));

function accepts(src, flags = 'u') {
  try { new RegExp(src, flags); return true; } catch { return false; }
}

function normalize(ranges) {
  ranges.sort((a, b) => a[0] - b[0]);
  const out = [];
  for (const [lo, hi] of ranges) {
    if (out.length && lo <= out[out.length - 1][1] + 1) out[out.length - 1][1] = Math.max(out[out.length - 1][1], hi);
    else out.push([lo, hi]);
  }
  return out;
}

function scan(escape) {
  const ranges = [];
  const re = new RegExp(`(?:${escape})+`, 'gu');
  for (const m of ALL.matchAll(re)) {
    const end = m.index + m[0].length;
    const lo = cpAt(m.index), hi = cpAt(end > 0xf800 ? end - 2 : end - 1);
    if (lo < 0xd800 && hi >= 0xe000) ranges.push([lo, 0xd7ff], [0xe000, hi]);
    else ranges.push([lo, hi]);
  }
  const one = new RegExp(`^(?:${escape})$`, 'u');
  for (let s = 0xd800; s <= 0xdfff; s++) if (one.test(String.fromCharCode(s))) ranges.push([s, s]);
  return normalize(ranges);
}

const same = (a, b) => a.length === b.length && a.every((r, i) => r[0] === b[i][0] && r[1] === b[i][1]);
const union = (...sets) => normalize(sets.flat().map((r) => [...r]));
function xor(a, b) {
  // symmetric difference of two normalized range lists, through their boundary points
  const ev = [];
  for (const [lo, hi] of [...a, ...b]) ev.push([lo, 1], [hi + 1, -1]);
  ev.sort((x, y) => x[0] - y[0]);
  const out = [];
  let depth = 0, start = 0, i = 0;
  while (i < ev.length) {
    const p = ev[i][0];
    const before = depth % 2;
    while (i < ev.length && ev[i][0] === p) depth += ev[i++][1];
    const after = depth % 2;
    if (!before && after) start = p;
    else if (before && !after) out.push([start, p - 1]);
  }
  return normalize(out);
}

// ---------------------------------------------------------------- sets and names
const sets = [];  // range lists; the index is the set number in the generated table
const setIndex = (ranges) => { sets.push(ranges); return sets.length - 1; };
const skipped = [];

const gcLeafSet = {};
const gcEntries = [];  // [name, mask]
GC_LEAVES.forEach((names, i) => {
  const r = scan(`\\p{${names[0]}}`);
  gcLeafSet[names[0]] = { index: setIndex(r), bit: i, ranges: r };
  for (const n of names) {
    for (const form of [`\\p{${n}}`, `\\p{gc=${n}}`, `\\p{General_Category=${n}}`])
      if (!accepts(form)) throw new Error(`Node.js rejects ${form}`);
    if (!same(scan(`\\p{gc=${n}}`), r)) throw new Error(`gc=${n} differs`);
    gcEntries.push([n, 1 << i]);
  }
});
for (const [names, leaves] of GC_GROUPS) {
  const want = union(...leaves.map((l) => gcLeafSet[l].ranges));
  let mask = 0;
  for (const l of leaves) mask |= 1 << gcLeafSet[l].bit;
  for (const n of names) {
    if (!accepts(`\\p{${n}}`)) throw new Error(`Node.js rejects \\p{${n}}`);
    if (!same(scan(`\\p{${n}}`), want)) throw new Error(`\\p{${n}} is not the union of ${leaves}`);
    gcEntries.push([n, mask >>> 0]);
  }
}

const scriptEntries = [];  // [name, scSet, scxDiffSet]
for (const names of SCRIPTS) {
  if (!accepts(`\\p{sc=${names[0]}}`)) { skipped.push(`sc=${names.join('/')}`); continue; }
  const sc = scan(`\\p{sc=${names[0]}}`);
  const scx = scan(`\\p{scx=${names[0]}}`);
  const scIndex = setIndex(sc), diffIndex = setIndex(xor(sc, scx));
  for (const n of names) {
    for (const form of [`\\p{sc=${n}}`, `\\p{Script=${n}}`, `\\p{scx=${n}}`, `\\p{Script_Extensions=${n}}`])
      if (!accepts(form)) throw new Error(`Node.js rejects ${form}`);
    if (accepts(`\\p{${n}}`)) throw new Error(`Node.js accepts the lone script name ${n}`);
    scriptEntries.push([n, scIndex, diffIndex]);
  }
}
// every four-letter script code that Node.js accepts must be in SCRIPTS
{
  const known = new Set(scriptEntries.map((e) => e[0]));
  const A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', a = 'abcdefghijklmnopqrstuvwxyz';
  for (const c0 of A) for (const c1 of a) for (const c2 of a) for (const c3 of a) {
    const code = c0 + c1 + c2 + c3;
    if (!known.has(code) && accepts(`\\p{sc=${code}}`)) throw new Error(`unknown script code ${code}`);
  }
}

const binaryEntries = [];  // [name, set]; set 0xffff = Any, 0xfffe = ASCII, 0xfffd = Assigned
const SPECIAL = { Any: 0xffff, ASCII: 0xfffe, Assigned: 0xfffd };
for (const [n, id] of Object.entries(SPECIAL)) {
  if (!accepts(`\\p{${n}}`)) throw new Error(`Node.js rejects \\p{${n}}`);
  binaryEntries.push([n, id]);
}
if (!same(scan('\\p{Any}'), [[0, 0x10ffff]]) || !same(scan('\\p{ASCII}'), [[0, 0x7f]]) ||
    !same(scan('\\p{Assigned}'), xor([[0, 0x10ffff]], gcLeafSet.Cn.ranges)))
  throw new Error('Any / ASCII / Assigned are not what the engine computes');
for (const names of BINARY) {
  const ok = names.filter((n) => accepts(`\\p{${n}}`));
  for (const n of names) if (!ok.includes(n)) skipped.push(n);
  if (!ok.length) continue;
  const r = scan(`\\p{${ok[0]}}`);
  const index = setIndex(r);
  for (const n of ok) {
    if (n !== ok[0] && !same(scan(`\\p{${n}}`), r)) throw new Error(`${n} differs from ${ok[0]}`);
    binaryEntries.push([n, index]);
  }
}
for (const n of ['gc', 'sc', 'scx', 'Script', 'General_Category', 'Basic_Emoji', 'RGI_Emoji', 'Latin', 'L&'])
  if (accepts(`\\p{${n}}`)) throw new Error(`Node.js accepts \\p{${n}}, which the engine rejects`);

// ---------------------------------------------------------------- case-insensitive equivalence
// Non-unicode /i: Canonicalize (ES2024 22.2.2.7.3), computed as the spec states with toUpperCase.
function canonNonUnicode(c) {
  const u = String.fromCharCode(c).toUpperCase();
  if (u.length !== 1) return c;
  const cu = u.charCodeAt(0);
  return c >= 128 && cu < 128 ? c : cu;
}
const hex = (c) => `\\u{${c.toString(16)}}`;
const nonUnicodeRep = new Map();  // code unit -> canonical value, for the units that change
for (let c = 0; c <= 0xffff; c++) {
  const r = canonNonUnicode(c);
  if (r !== c) nonUnicodeRep.set(c, r);
}
{
  // verify every class with V8: all members match each other, and nothing else with a candidate
  const classes = new Map();
  for (let c = 0; c <= 0xffff; c++) {
    const r = nonUnicodeRep.get(c) ?? c;
    if (!classes.has(r)) classes.set(r, []);
    classes.get(r).push(c);
  }
  for (const members of classes.values()) {
    if (members.length < 2) continue;
    for (const a of members) {
      const re = new RegExp(`^${a < 0xd800 || a > 0xdfff ? String.fromCharCode(a).replace(/[\\^$.*+?()[\]{}|/]/g, '\\$&') : `\\u${a.toString(16).padStart(4, '0')}`}$`, 'i');
      for (const b of members) if (!re.test(String.fromCharCode(b))) throw new Error(`/i: ${a} does not match ${b}`);
    }
  }
  for (let c = 0; c <= 0xffff; c++) {
    // also the full mappings: a candidate outside the class must not match
    const s = String.fromCharCode(c);
    for (const t of [s.toLowerCase(), s.toUpperCase()]) {
      if (t.length !== 1 || t === s) continue;
      const same = (nonUnicodeRep.get(c) ?? c) === (nonUnicodeRep.get(t.charCodeAt(0)) ?? t.charCodeAt(0));
      const re = new RegExp(`^\\u${c.toString(16).padStart(4, '0')}$`, 'i');
      if (re.test(t) !== same) throw new Error(`/i: ${c} vs ${t.charCodeAt(0)}`);
    }
  }
}

// Unicode /iu: simple case folding classes; candidates from the case mappings, verified with V8.
const parent = new Int32Array(0x110000).map((_, i) => i);
const find = (x) => { while (parent[x] !== x) x = parent[x] = parent[parent[x]]; return x; };
const join = (a, b) => { a = find(a); b = find(b); if (a !== b) parent[Math.max(a, b)] = Math.min(a, b); };
{
  const byKey = new Map();
  for (let cp = 0; cp <= 0x10ffff; cp++) {
    if (cp >= 0xd800 && cp <= 0xdfff) continue;
    const s = String.fromCodePoint(cp);
    const l = s.toLowerCase(), u = s.toUpperCase();
    if (l === s && u === s) continue;
    for (const k of new Set([l, u, u.toLowerCase(), l.toUpperCase()])) {
      if (k === s) continue;
      const kc = k.codePointAt(0);
      if (k.length === (kc > 0xffff ? 2 : 1)) join(cp, kc);
      else if (byKey.has(k)) join(cp, byKey.get(k));
      else byKey.set(k, cp);
    }
  }
}
const candidates = new Map();
for (let cp = 0; cp <= 0x10ffff; cp++) {
  const r = find(cp);
  if (r === cp) continue;
  if (!candidates.has(r)) candidates.set(r, [r]);
  candidates.get(r).push(cp);
}
const unicodeClasses = [];  // verified classes, each sorted
for (const members of candidates.values()) {
  // split the candidate group into the classes V8 sees (equivalence is transitive)
  const left = [...members];
  while (left.length) {
    const a = left.shift();
    const re = new RegExp(`^${hex(a)}$`, 'iu');
    const cls = [a];
    for (let i = 0; i < left.length; i++) if (re.test(String.fromCodePoint(left[i]))) cls.push(...left.splice(i--, 1));
    for (const x of cls) for (const y of cls) if (!new RegExp(`^${hex(x)}$`, 'iu').test(String.fromCodePoint(y))) throw new Error(`/iu: ${x} ${y} not transitive`);
    if (cls.length > 1) unicodeClasses.push(cls.sort((p, q) => p - q));
  }
}
const unicodeRep = new Map();
for (const cls of unicodeClasses) {
  // the representative is the simple lowercase when it is in the class (compact runs), else the smallest
  let rep = cls[0];
  for (const c of cls) {
    const l = String.fromCodePoint(c).toLowerCase();
    if (l.length <= 2 && l.codePointAt(0) !== c && cls.includes(l.codePointAt(0)) && String.fromCodePoint(l.codePointAt(0)) === l) { rep = l.codePointAt(0); break; }
  }
  for (const c of cls) if (c !== rep) unicodeRep.set(c, rep);
}

// runs of (code point -> representative) with a constant delta and a stride of 1 or 2
function runs(map) {
  const keys = [...map.keys()].sort((a, b) => a - b);
  const out = [];
  for (const k of keys) {
    const d = map.get(k) - k;
    const last = out[out.length - 1];
    if (last && last.delta === d && last.count < 0xffff) {
      const end = last.lo + (last.count - 1) * last.stride;
      if (last.count === 1 && (k - end === 1 || k - end === 2)) { last.stride = k - end; last.count++; continue; }
      if (k - end === last.stride) { last.count++; continue; }
    }
    out.push({ lo: k, count: 1, stride: 1, delta: d });
  }
  return out;
}
const foldNonUnicode = runs(nonUnicodeRep);
const foldUnicode = runs(unicodeRep);

// ---------------------------------------------------------------- output
function varint(out, v) {
  while (v >= 0x80) { out.push((v & 0x7f) | 0x80); v >>>= 7; }
  out.push(v);
}
const data = [], offsets = [];
for (const s of sets) {
  offsets.push(data.length);
  let prev = 0;
  for (const [lo, hi] of s) { varint(data, lo - prev); varint(data, hi - lo); prev = hi + 1; }
}
offsets.push(data.length);

const lines = [];
const emit = (s) => lines.push(s);
const list = (arr, per) => {
  for (let i = 0; i < arr.length; i += per) emit('  ' + arr.slice(i, i + per).join(', ') + ',');
};
const icu = process.versions.icu, uni = process.versions.unicode;
emit(`// Generated by scripts/gen-regexp-unicode.mjs from Node.js ${process.version} (V8 ${process.versions.v8},`);
emit(`// ICU ${icu}, Unicode ${uni}). Do not edit; run the script again to update.`);
emit('// Included by src/regexp/unicode.cpp only.');
emit('');
emit(`static const char kUnicodeVersion[] = "${uni}";`);
emit('');
emit('// Code point sets: per set, (gap from the previous range end + 1, range length - 1) pairs as LEB128');
emit('// varints; kSetOffsets[i] .. kSetOffsets[i + 1] delimit set i in kSetData.');
emit(`static const uint8_t kSetData[${data.length}] = {`);
list(data, 24);
emit('};');
emit(`static const uint32_t kSetOffsets[${offsets.length}] = {`);
list(offsets, 12);
emit('};');
emit('');
emit('// General_Category leaves, in the bit order of GcName::mask');
emit(`static const uint16_t kGcLeafSets[${GC_LEAVES.length}] = {`);
list(GC_LEAVES.map((n) => gcLeafSet[n[0]].index), 16);
emit('};');
emit(`static const GcName kGcNames[${gcEntries.length}] = {`);
for (const [n, m] of gcEntries) emit(`  {"${n}", 0x${m.toString(16)}u},`);
emit('};');
emit('// Script values: the Script set and the Script_Extensions set as a symmetric difference with it');
emit(`static const ScriptName kScriptNames[${scriptEntries.length}] = {`);
for (const [n, sc, d] of scriptEntries) emit(`  {"${n}", ${sc}, ${d}},`);
emit('};');
emit('// Binary properties; kSetAny / kSetAscii / kSetAssigned are computed');
emit(`static const BinaryName kBinaryNames[${binaryEntries.length}] = {`);
for (const [n, s] of binaryEntries) emit(`  {"${n}", ${s === 0xffff ? 'kSetAny' : s === 0xfffe ? 'kSetAscii' : s === 0xfffd ? 'kSetAssigned' : s}},`);
emit('};');
emit('');
emit('// Case-insensitive representatives: code points lo, lo + stride, ... (count of them) map to');
emit('// themselves + delta; code points in no run are their own representative.');
emit('// Without the u flag: Canonicalize (toUpperCase rule, ES2024 22.2.2.7.3) on code units.');
emit(`static const FoldRun kFoldNonUnicode[${foldNonUnicode.length}] = {`);
for (const r of foldNonUnicode) emit(`  {0x${r.lo.toString(16)}, ${r.count}, ${r.stride}, ${r.delta}},`);
emit('};');
emit('// With the u flag: simple case folding classes as V8 (ICU) computes them.');
emit(`static const FoldRun kFoldUnicode[${foldUnicode.length}] = {`);
for (const r of foldUnicode) emit(`  {0x${r.lo.toString(16)}, ${r.count}, ${r.stride}, ${r.delta}},`);
emit('};');
const text = lines.join('\n') + '\n';

const size = data.length + offsets.length * 4 + GC_LEAVES.length * 2 + (foldNonUnicode.length + foldUnicode.length) * 12 +
  (gcEntries.length + scriptEntries.length + binaryEntries.length) * 16;
const summary = `${sets.length} sets, ${data.length} bytes of ranges; ${gcEntries.length} gc / ${scriptEntries.length} script / ` +
  `${binaryEntries.length} binary names; ${foldNonUnicode.length} + ${foldUnicode.length} fold runs ` +
  `(${unicodeClasses.length} /iu classes); about ${(size / 1024).toFixed(1)} KB of data plus name strings`;

if (process.argv.includes('--check')) {
  const old = fs.existsSync(outFile) ? fs.readFileSync(outFile, 'utf8') : '';
  if (old !== text) { console.error(`${outFile} is out of date (${summary})`); process.exit(1); }
  console.log(`up to date: ${summary}`);
} else {
  fs.writeFileSync(outFile, text);
  console.log(`wrote ${path.relative(root, outFile)}: ${summary}`);
}
if (skipped.length) console.log(`not supported by this Node.js: ${skipped.join(' ')}`);
