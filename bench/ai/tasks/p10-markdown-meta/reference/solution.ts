// E7 port of original.ts: blog post metadata from Markdown (title, slug, excerpt, table of contents,
// word count, reading time, outgoing links). The original's regular expressions are small scanners here.

interface TocEntry {
  level: number;
  text: string;
  id: string;
}

// One interface for headings and text lines (level is 0 for text).
interface Block {
  kind: string; // "heading" | "text"
  level: number;
  text: string;
}

interface Meta {
  title: string;
  slug: string;
  excerpt: string;
  toc: TocEntry[];
  words: number;
  readingMinutes: number;
  links: string[];
}

const EXCERPT_LENGTH = 120;
const WORDS_PER_MINUTE = 200;

const isSpace = (c: string) => c.trim() === ""; // \s
const isDigit = (c: string) => c >= "0" && c <= "9";
const isAlnum = (c: string) => isDigit(c) || (c >= "a" && c <= "z") || (c >= "A" && c <= "Z");

function collapseSpaces(s: string): string {
  return s.split("").map((c) => (isSpace(c) ? " " : c)).join("").split(" ").filter((w) => w !== "").join(" ");
}

// ![alt](...) -> alt (image = true), [text](...) -> text (image = false)
function replaceLinks(s: string, image: boolean): string {
  let out = "";
  let i = 0;
  while (i < s.length) {
    const start = image ? (s.startsWith("![", i) ? i + 1 : -1) : s[i] === "[" ? i : -1;
    if (start >= 0) {
      const close = s.indexOf("]", start + 1);
      const minText = image ? 0 : 1;
      if (close >= start + 1 + minText && s.charAt(close + 1) === "(") {
        const end = s.indexOf(")", close + 2);
        if (end >= 0) {
          out += s.slice(start + 1, close);
          i = end + 1;
          continue;
        }
      }
    }
    out += s[i];
    i++;
  }
  return out;
}

// open + at least one character + close -> the characters; with drop, the whole match is removed.
// lazy: like /open(.+?)close/g (the text may contain the close mark), otherwise like /open([^close]+)close/g
function replaceDelimited(s: string, open: string, close: string, lazy: boolean, drop: boolean): string {
  let out = "";
  let i = 0;
  while (i < s.length) {
    if (s.startsWith(open, i)) {
      const end = s.indexOf(close, lazy ? i + open.length + 1 : i + open.length);
      if (end > i + open.length) {
        if (!drop) out += s.slice(i + open.length, end);
        i = end + close.length;
        continue;
      }
    }
    out += s[i];
    i++;
  }
  return out;
}

function plain(markdown: string): string {
  let text = replaceLinks(markdown, true);
  text = replaceLinks(text, false);
  text = replaceDelimited(text, "`", "`", false, false);
  text = replaceDelimited(text, "**", "**", true, false);
  text = replaceDelimited(text, "*", "*", true, false);
  text = replaceDelimited(text, "<", ">", false, true);
  return collapseSpaces(text);
}

// links that are not images: [text](url "title") -> url
function collectLinks(line: string, links: string[]): void {
  let i = 0;
  while (i < line.length) {
    if (line[i] === "[" && (i === 0 || line[i - 1] !== "!")) {
      const close = line.indexOf("]", i + 1);
      if (close > i + 1 && line.charAt(close + 1) === "(") {
        let u = close + 2;
        while (u < line.length && line[u] !== ")" && !isSpace(line[u])) u++;
        const end = line.indexOf(")", u);
        if (u > close + 2 && end >= 0) {
          const url = line.slice(close + 2, u);
          if (!links.includes(url)) links.push(url);
          i = end + 1;
          continue;
        }
      }
    }
    i++;
  }
}

function isRule(line: string): boolean {
  const marks = [...line].filter((c) => !isSpace(c));
  return marks.length >= 3 && (marks[0] === "-" || marks[0] === "*" || marks[0] === "_") && marks.every((c) => c === marks[0]);
}

// strips "> " and then a list marker ("- ", "* ", "+ ", "1. ")
function stripPrefixes(raw: string): string {
  let line = raw;
  const t = line.trimStart();
  if (t.startsWith(">")) line = t.length > 1 && isSpace(t[1]) ? t.slice(2) : t.slice(1);
  const u = line.trimStart();
  let n = 0;
  if (u.length > 0 && (u[0] === "-" || u[0] === "*" || u[0] === "+")) n = 1;
  else {
    while (n < u.length && isDigit(u[n])) n++;
    n = n > 0 && u.charAt(n) === "." ? n + 1 : 0;
  }
  if (n > 0 && n < u.length && isSpace(u[n])) {
    let k = n;
    while (k < u.length && isSpace(u[k])) k++;
    return u.slice(k);
  }
  return line;
}

function slugify(text: string): string {
  let kept = "";
  for (const c of text.toLowerCase()) {
    if (isAlnum(c) || c === "-") kept += c;
    else if (isSpace(c)) kept += " ";
  }
  const parts = kept.trim().split("").map((c) => (c === " " ? "-" : c)).join("").split("-");
  return parts.filter((p) => p !== "").join("-");
}

class MarkdownDoc {
  blocks: Block[] = [];
  links: string[] = [];

  constructor(source: string) {
    let inFence = false;
    for (const line of source.split("\n")) {
      const raw = line.endsWith("\r") ? line.slice(0, -1) : line;
      if (raw.trimStart().startsWith("```")) {
        inFence = !inFence;
        continue;
      }
      if (inFence || raw.trim() === "") continue;
      if (isRule(raw)) continue;
      collectLinks(raw, this.links);
      let hashes = 0;
      while (hashes < raw.length && raw[hashes] === "#") hashes++;
      if (hashes >= 1 && hashes <= 6 && hashes < raw.length && isSpace(raw[hashes])) {
        this.blocks.push({ kind: "heading", level: hashes, text: plain(raw.slice(hashes)) });
        continue;
      }
      this.blocks.push({ kind: "text", level: 0, text: plain(stripPrefixes(raw)) });
    }
  }

  get title(): string {
    const headings = this.blocks.filter((b) => b.kind === "heading");
    const h1 = headings.find((b) => b.level === 1);
    const first = h1 ?? (headings.length > 0 ? headings[0] : undefined);
    return first?.text || "Untitled";
  }

  get words(): number {
    const tokens = this.blocks.map((b) => b.text).join(" ").split(" ");
    return tokens.filter((w) => [...w].some((c) => isAlnum(c))).length;
  }

  get excerpt(): string {
    const text = this.blocks.filter((b) => b.kind === "text").map((b) => b.text).join(" ");
    if (text.length <= EXCERPT_LENGTH) return text;
    const cut = text.slice(0, EXCERPT_LENGTH);
    // drop the last (possibly partial) word: /\s+\S*$/
    let k = cut.length;
    while (k > 0 && !isSpace(cut[k - 1])) k--;
    if (k === 0) return cut + "...";
    while (k > 0 && isSpace(cut[k - 1])) k--;
    return cut.slice(0, k) + "...";
  }

  toc(): TocEntry[] {
    const seen = new Map<string, number>();
    const entries: TocEntry[] = [];
    for (const block of this.blocks) {
      if (block.kind !== "heading" || block.level < 2 || block.level > 3) continue;
      const base = slugify(block.text) || "section";
      const n = seen.get(base);
      entries.push({ level: block.level, text: block.text, id: n === undefined ? base : `${base}-${n}` });
      seen.set(base, (n ?? 0) + 1);
    }
    return entries;
  }
}

// Called by the CMS preview and the app with the Markdown source of a post.
export function articleMeta(markdown: string): string {
  const doc = new MarkdownDoc(markdown);
  const words = doc.words;
  const meta: Meta = {
    title: doc.title,
    slug: slugify(doc.title) || "untitled",
    excerpt: doc.excerpt,
    toc: doc.toc(),
    words,
    readingMinutes: Math.max(1, Math.ceil(words / WORDS_PER_MINUTE)),
    links: doc.links,
  };
  return JSON.stringify(meta);
}
