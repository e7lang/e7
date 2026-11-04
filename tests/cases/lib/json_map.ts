// E7 extension: JSON objects with dynamic keys parse into Map<string, T>
const m = JSON.parse('{"net": 10.5, "tax": 2.1, "n": -1}') as Map<string, number>;
console.log(m.get("net"), m.get("tax"), m.size, [...m.keys()].join(","));
const nested = JSON.parse('{"a": {"x": [1, 2]}, "b": {}}') as Map<string, Map<string, number[]>>;
console.log(nested.get("a")!.get("x")!.length, nested.get("b")!.size);
