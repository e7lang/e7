const a: readonly number[] = [3, 1, 2];
console.log(a.toSorted(), a.toSorted((x, y) => y - x), a.toReversed(), a.with(0, 9), a.with(-1, 7), a);
console.log(a.toSpliced(1, 1), a.toSpliced(1, 0, 8, 9), a.toSpliced(1), ["b", "a"].toSorted());
try { a.with(5, 1); } catch (e) { console.log(String(e)); }
