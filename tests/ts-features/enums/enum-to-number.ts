// @feature: Enum values as `number` / `string`
// @category: Enums
// @status: supported
enum Color { Red, Green, Blue }
enum Mode { Fast = "fast" }
const n: number = Color.Blue;
const s: string = Mode.Fast;
const colors = new Map<Color, string>([[Color.Red, "#f00"], [Color.Green, "#0f0"]]);
console.log(n + 1, s.toUpperCase(), colors.get(Color.Green), colors);
