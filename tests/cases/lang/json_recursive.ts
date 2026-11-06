import { tryParse } from "e7:json";
interface Tree { label: string; children: Tree[] }
interface Node2 { value: number; next: Node2 | null }
interface A { b?: B; name: string }
interface B { a: A; n: number }
const t = JSON.parse('{"label":"root","children":[{"label":"x","children":[]},{"label":"y","children":[{"label":"z","children":[]}]}]}') as Tree;
console.log(t.children[1].children[0].label, JSON.stringify(t));
const list = JSON.parse('{"value":1,"next":{"value":2,"next":null}}') as Node2;
console.log(list.next?.value, list.next?.next);
const ab = JSON.parse('{"b":{"a":{"name":"inner"},"n":1},"name":"top"}') as A;
console.log(ab.b?.a.name, ab);
console.log(tryParse('{"label":"a","children":[]}') as Tree | null);
