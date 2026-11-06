// @error: property 'line' has no getter
const sink = {
  set line(s: string) {
    console.log(s);
  },
};
console.log(sink.line);
