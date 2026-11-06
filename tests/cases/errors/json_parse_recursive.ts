// @error: JSON.parse into the recursive type 'Tree' is not supported yet
interface Tree { label: string; children: Tree[] }
const t = JSON.parse('{"label":"x","children":[]}') as Tree;
console.log(t.label);
