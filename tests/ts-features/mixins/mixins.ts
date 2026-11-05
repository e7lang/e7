// @feature: Mixins (functions returning class expressions)
// @category: Mixins
// @status: unsupported
// @why: not implemented yet: class expressions and constructor types
// @instead: composition (a field holding the helper object) or a common base class
// @error: expected ';' but found '('
type Constructor = new (...args: any[]) => {};
function Scale<TBase extends Constructor>(Base: TBase) {
  return class Scaling extends Base {
    scale = 1;
    setScale(s: number): void { this.scale = s; }
  };
}
class Sprite { name = "sprite"; }
const EightBitSprite = Scale(Sprite);
const s = new EightBitSprite();
s.setScale(0.8);
console.log(s.scale);
