// @feature: Recursive types
// @category: Object Types
// @status: partial
// @note: classes may refer to themselves (linked lists, trees)
class TreeNode {
  children: TreeNode[] = [];
  constructor(public label: string) {}
  add(child: TreeNode): TreeNode { this.children.push(child); return this; }
  size(): number { return 1 + this.children.reduce((n, c) => n + c.size(), 0); }
}
const root = new TreeNode("root").add(new TreeNode("a")).add(new TreeNode("b").add(new TreeNode("c")));
console.log(root.size(), root.children[1].children[0].label);
