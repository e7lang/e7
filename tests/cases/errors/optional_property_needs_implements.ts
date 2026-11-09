// @error: write 'class Circle implements Shape'
interface Shape { size: number; label?: string }
class Circle { constructor(public size: number) {} }
const s: Shape = new Circle(2);
console.log(s.size);
