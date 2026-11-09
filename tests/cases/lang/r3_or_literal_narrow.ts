// third test round of 0.1.3 (or_literal_narrow): must print what Node.js prints
type Env = "dev" | "staging" | "prod";
function parse(s: string | undefined): Env | undefined {
  if (s === "dev" || s === "staging" || s === "prod") return s;
  return undefined;
}
function parse2(s: string): Env {
  if (s === "staging" || s === "prod") return s;
  return "dev";
}
console.log(parse("prod"), parse("x"), parse(undefined), parse2("staging"), parse2("q"));
