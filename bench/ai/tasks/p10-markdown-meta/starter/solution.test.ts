import { assertEqual } from "e7:test";
import { articleMeta } from "./solution.ts";

export function testShortPost(): void {
  assertEqual(articleMeta('# Hello, World!\n\nMy **first** post.\n\n## Next steps\nMore soon.'), '{"title":"Hello, World!","slug":"hello-world","excerpt":"My first post. More soon.","toc":[{"level":2,"text":"Next steps","id":"next-steps"}],"words":9,"readingMinutes":1,"links":[]}');
}

export function testNoHeading(): void {
  assertEqual(articleMeta('Just a note with a [link](https://example.com).'), '{"title":"Untitled","slug":"untitled","excerpt":"Just a note with a link.","toc":[],"words":6,"readingMinutes":1,"links":["https://example.com"]}');
}
