// Builds the metadata of a blog post from its Markdown source: title, slug, plain-text excerpt,
// table of contents with unique anchors, word count, reading time and outgoing links.

export interface TocEntry {
  level: number;
  text: string;
  id: string;
}

interface Summary {
  readonly title: string;
  readonly words: number;
}

type Block = { kind: "heading"; level: number; text: string } | { kind: "text"; text: string };

const EXCERPT_LENGTH = 120;
const WORDS_PER_MINUTE = 200;

const INLINE_RULES: [RegExp, string][] = [
  [/!\[([^\]]*)\]\([^)]*\)/g, "$1"], // images -> alt text
  [/\[([^\]]+)\]\([^)]*\)/g, "$1"], // links -> link text
  [/`([^`]+)`/g, "$1"], // inline code
  [/\*\*(.+?)\*\*/g, "$1"], // bold
  [/\*(.+?)\*/g, "$1"], // italics
  [/<[^>]+>/g, ""], // inline HTML
];

export function slugify(text: string): string {
  return text
    .toLowerCase()
    .replace(/[^a-z0-9\s-]/g, "")
    .trim()
    .replace(/[\s-]+/g, "-")
    .replace(/^-+|-+$/g, "");
}

class MarkdownDoc implements Summary {
  blocks: Block[] = [];
  links: string[] = [];

  constructor(source: string) {
    let inFence = false;
    for (const raw of source.split(/\r?\n/)) {
      if (/^\s*```/.test(raw)) {
        inFence = !inFence;
        continue;
      }
      if (inFence || /^\s*$/.test(raw)) continue;
      if (/^\s*([-*_])(\s*\1){2,}\s*$/.test(raw)) continue; // horizontal rule
      for (const m of raw.matchAll(/(?<!!)\[[^\]]+\]\(([^)\s]+)[^)]*\)/g)) {
        if (!this.links.includes(m[1])) this.links.push(m[1]);
      }
      const heading = raw.match(/^(#{1,6})\s+(.*)$/);
      if (heading) {
        this.blocks.push({ kind: "heading", level: heading[1].length, text: this.plain(heading[2]) });
        continue;
      }
      const line = raw.replace(/^\s*>\s?/, "").replace(/^\s*([-*+]|\d+\.)\s+/, "");
      this.blocks.push({ kind: "text", text: this.plain(line) });
    }
  }

  plain(markdown: string): string {
    let text = markdown;
    for (const [re, replacement] of INLINE_RULES) text = text.replace(re, replacement);
    return text.replace(/\s+/g, " ").trim();
  }

  get title(): string {
    const headings = this.blocks.filter((b) => b.kind === "heading");
    const h1 = headings.find((b) => b.kind === "heading" && b.level === 1);
    return (h1 ?? headings[0])?.text || "Untitled";
  }

  get words(): number {
    return this.blocks
      .map((b) => b.text)
      .join(" ")
      .split(/\s+/)
      .filter(this.isWord).length;
  }

  isWord(token: string): boolean {
    return /[a-z0-9]/i.test(token);
  }

  get excerpt(): string {
    const text = this.blocks.filter((b) => b.kind === "text").map((b) => b.text).join(" ");
    if (text.length <= EXCERPT_LENGTH) return text;
    return text.slice(0, EXCERPT_LENGTH).replace(/\s+\S*$/, "") + "...";
  }

  toc(): TocEntry[] {
    const seen: Record<string, number> = {};
    const entries: TocEntry[] = [];
    for (const block of this.blocks) {
      if (block.kind !== "heading" || block.level < 2 || block.level > 3) continue;
      const base = slugify(block.text) || "section";
      const id = base in seen ? `${base}-${seen[base]}` : base;
      seen[base] = (seen[base] ?? 0) + 1;
      entries.push({ level: block.level, text: block.text, id });
    }
    return entries;
  }
}

// Called by the CMS preview and the app with the Markdown source of a post.
export function articleMeta(markdown: string): string {
  const doc = new MarkdownDoc(markdown);
  return JSON.stringify({
    title: doc.title,
    slug: slugify(doc.title) || "untitled",
    excerpt: doc.excerpt,
    toc: doc.toc(),
    words: doc.words,
    readingMinutes: Math.max(1, Math.ceil(doc.words / WORDS_PER_MINUTE)),
    links: doc.links,
  });
}
