// fourth test round of 0.1.3 (types/static_nullable_inc_ice): must print what Node.js prints
class S { static n: number | null = 1; }
if (S.n !== null) { S.n++; }
console.log(S.n);
