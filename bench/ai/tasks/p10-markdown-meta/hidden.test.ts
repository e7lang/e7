import { assertEqual } from "e7:test";
import { articleMeta } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

export function testFullPost(): void {
  const md = '# Shipping *E7* scripts: a **practical** guide\n\nE7 compiles a strict subset of [TypeScript](https://www.typescriptlang.org/docs/ "TS docs") to bytecode.\nThis post shows how we ship `pricing.ts` to the apps.\n\n## Why bytecode?\n\n> Smaller, verified, and *fast* to load.\n\n## Setup\n\n- Install the [toolchain](https://github.com/e7lang/e7/releases)\n- Run `e7c --check`\n1. Read the [docs](https://e7lang.github.io/) again\n\n```ts\nexport function main(): string { return "hi"; }\n## not a heading\n```\n\n---\n\n### Setup\n\n![diagram of the pipeline](img/pipeline.png)\n\n## Why bytecode?\nDone. See the [docs](https://e7lang.github.io/).';
  assertEqual(articleMeta(md), '{"title":"Shipping E7 scripts: a practical guide","slug":"shipping-e7-scripts-a-practical-guide","excerpt":"E7 compiles a strict subset of TypeScript to bytecode. This post shows how we ship pricing.ts to the apps. Smaller,...","toc":[{"level":2,"text":"Why bytecode?","id":"why-bytecode"},{"level":2,"text":"Setup","id":"setup"},{"level":3,"text":"Setup","id":"setup-1"},{"level":2,"text":"Why bytecode?","id":"why-bytecode-1"}],"words":55,"readingMinutes":1,"links":["https://www.typescriptlang.org/docs/","https://github.com/e7lang/e7/releases","https://e7lang.github.io/"]}');
}

export function testExcerptCutAtWordBoundary(): void {
  const md = '# Long\n\nword word word word word word word word word word word word word word word word word word word word word word word word word word word word word word end\n\nxxxxxxxxxxxxxxxxxxxx tail';
  assertEqual(articleMeta(md), '{"title":"Long","slug":"long","excerpt":"word word word word word word word word word word word word word word word word word word word word word word word word...","toc":[],"words":34,"readingMinutes":1,"links":[]}');
}

export function testExcerptExactlyAtLimit(): void {
  const md = '# Limit\naaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa';
  assertEqual(articleMeta(md), '{"title":"Limit","slug":"limit","excerpt":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","toc":[],"words":2,"readingMinutes":1,"links":[]}');
}

export function testExcerptWithoutSpaces(): void {
  const md = '# Limit\nbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb';
  assertEqual(articleMeta(md), '{"title":"Limit","slug":"limit","excerpt":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb...","toc":[],"words":2,"readingMinutes":1,"links":[]}');
}

export function testTitlePrefersLevelOne(): void {
  assertEqual(articleMeta('## Intro\ntext\n# Real Title\n### Détails & Notes\n#### Deep'), '{"title":"Real Title","slug":"real-title","excerpt":"text","toc":[{"level":2,"text":"Intro","id":"intro"},{"level":3,"text":"Détails & Notes","id":"dtails-notes"}],"words":7,"readingMinutes":1,"links":[]}');
}

export function testDuplicateAndEmptySlugs(): void {
  assertEqual(articleMeta('# T\n## FAQ\n## faq\n## F.A.Q.\n## ???\n## !!!\n### FAQ-1'), '{"title":"T","slug":"t","excerpt":"","toc":[{"level":2,"text":"FAQ","id":"faq"},{"level":2,"text":"faq","id":"faq-1"},{"level":2,"text":"F.A.Q.","id":"faq-2"},{"level":2,"text":"???","id":"section"},{"level":2,"text":"!!!","id":"section-1"},{"level":3,"text":"FAQ-1","id":"faq-1"}],"words":5,"readingMinutes":1,"links":[]}');
}

export function testCrLfAndHorizontalRules(): void {
  assertEqual(articleMeta('# Rules\r\n\r\n* * *\r\n___\r\n- - - -\r\n--\r\nafter the rules\r\n'), '{"title":"Rules","slug":"rules","excerpt":"-- after the rules","toc":[],"words":4,"readingMinutes":1,"links":[]}');
}

export function testHtmlAndNestedEmphasis(): void {
  assertEqual(articleMeta('# <span>Tagged</span> title\nSome <b>bold</b> and ***very*** strong *it* text, 2*3*4.'), '{"title":"Tagged title","slug":"tagged-title","excerpt":"Some bold and very strong it text, 234.","toc":[],"words":10,"readingMinutes":1,"links":[]}');
}

export function testUnclosedFenceHidesRest(): void {
  assertEqual(articleMeta('# Code\nintro\n  ```js\n## hidden\nmore hidden'), '{"title":"Code","slug":"code","excerpt":"intro","toc":[],"words":2,"readingMinutes":1,"links":[]}');
}

export function testEmptyDocument(): void {
  assertEqual(articleMeta(''), '{"title":"Untitled","slug":"untitled","excerpt":"","toc":[],"words":0,"readingMinutes":1,"links":[]}');
}

export function testHashWithoutSpaceAndSevenHashes(): void {
  assertEqual(articleMeta('#hashtag\n####### seven\n#  Spaced   Title  '), '{"title":"Spaced Title","slug":"spaced-title","excerpt":"#hashtag ####### seven","toc":[],"words":4,"readingMinutes":1,"links":[]}');
}

export function testReadingTime(): void {
  const md = '# Essay\nSentence number 0 has exactly five words.\nSentence number 1 has exactly five words.\nSentence number 2 has exactly five words.\nSentence number 3 has exactly five words.\nSentence number 4 has exactly five words.\nSentence number 5 has exactly five words.\nSentence number 6 has exactly five words.\nSentence number 7 has exactly five words.\nSentence number 8 has exactly five words.\nSentence number 9 has exactly five words.\nSentence number 10 has exactly five words.\nSentence number 11 has exactly five words.\nSentence number 12 has exactly five words.\nSentence number 13 has exactly five words.\nSentence number 14 has exactly five words.\nSentence number 15 has exactly five words.\nSentence number 16 has exactly five words.\nSentence number 17 has exactly five words.\nSentence number 18 has exactly five words.\nSentence number 19 has exactly five words.\nSentence number 20 has exactly five words.\nSentence number 21 has exactly five words.\nSentence number 22 has exactly five words.\nSentence number 23 has exactly five words.\nSentence number 24 has exactly five words.\nSentence number 25 has exactly five words.\nSentence number 26 has exactly five words.\nSentence number 27 has exactly five words.\nSentence number 28 has exactly five words.\nSentence number 29 has exactly five words.\nSentence number 30 has exactly five words.\nSentence number 31 has exactly five words.\nSentence number 32 has exactly five words.\nSentence number 33 has exactly five words.\nSentence number 34 has exactly five words.\nSentence number 35 has exactly five words.\nSentence number 36 has exactly five words.\nSentence number 37 has exactly five words.\nSentence number 38 has exactly five words.\nSentence number 39 has exactly five words.\nSentence number 40 has exactly five words.';
  assertEqual(articleMeta(md), '{"title":"Essay","slug":"essay","excerpt":"Sentence number 0 has exactly five words. Sentence number 1 has exactly five words. Sentence number 2 has exactly five...","toc":[],"words":288,"readingMinutes":2,"links":[]}');
}

export function testImageLinkNotCollectedAndDuplicateLinks(): void {
  const md = '# Links\n![logo](https://cdn.example.com/logo.png) [a](https://a.example) [b](https://a.example) [c](<bad>)';
  assertEqual(articleMeta(md), '{"title":"Links","slug":"links","excerpt":"logo a b c","toc":[],"words":5,"readingMinutes":1,"links":["https://a.example","<bad>"]}');
}
