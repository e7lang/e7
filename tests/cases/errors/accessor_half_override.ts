// @error: declares only the getter of 'v'
class Base { protected _v = 1; get v(): number { return this._v; } set v(x: number) { this._v = x; } }
class GetOnly extends Base { get v(): number { return this._v * 10; } }
console.log(new GetOnly().v);
