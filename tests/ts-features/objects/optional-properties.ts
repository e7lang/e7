// @feature: Optional properties
// @category: Object Types
// @status: supported
interface PaintOptions { shape: string; xPos?: number; yPos?: number }
function paint(opts: PaintOptions): string {
  const x = opts.xPos ?? 0;
  const y = opts.yPos === undefined ? 0 : opts.yPos;
  return `${opts.shape} at ${x},${y}`;
}
console.log(paint({ shape: "dot" }), paint({ shape: "box", xPos: 100 }), { shape: "o", yPos: 1 });
