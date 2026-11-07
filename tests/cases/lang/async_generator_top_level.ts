// top-level for await over an async generator; an exported async function that consumes one
async function* lines(text: string): AsyncGenerator<string> {
  for (const line of text.split("\n")) {
    await null;
    if (line.trim() !== "") yield line.trim();
  }
}

export async function wordCount(text: string): Promise<number> {
  let n = 0;
  for await (const line of lines(text)) n += line.split(" ").filter((w) => w !== "").length;
  return n;
}

const text = "one two\n\n  three four five \nsix";
let i = 0;
for await (const line of lines(text)) {
  console.log(++i, JSON.stringify(line));
}
console.log("words", await wordCount(text));
const gen = lines("a\nb");
const first = await gen.next();
console.log(first.value, first.done);
Promise.resolve().then(() => console.log("microtask"));
console.log("end of module");
