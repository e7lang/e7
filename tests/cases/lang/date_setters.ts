const d = new Date(Date.UTC(2024, 0, 31, 10, 20, 30, 400));
d.setMonth(1); console.log(d.toISOString());
d.setFullYear(2023, 11, 25); console.log(d.toISOString());
d.setDate(0); console.log(d.toISOString());
d.setHours(25, 61); console.log(d.toISOString());
d.setUTCMinutes(-1, 5, 7); console.log(d.toISOString());
d.setSeconds(90); d.setMilliseconds(1500); console.log(d.toISOString(), d.getTime());
const bad = new Date(NaN);
console.log(bad.setMonth(3), bad.setFullYear(2020), bad.toISOString());
