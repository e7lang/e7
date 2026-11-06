// @error: cannot assign to 'size' because it is a read-only property
const box = {
  items: [1, 2],
  get size(): number {
    return this.items.length;
  },
};
box.size++;
