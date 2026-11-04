const d = new Date(1700000000000);
console.log(d.toISOString(), d.getTime(), d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), d.getUTCDay());
console.log(d.getHours(), d.getMinutes(), d.getSeconds(), d.getMilliseconds(), d.getTimezoneOffset());
console.log(d.toUTCString());
console.log(d.toString());
console.log(d.toDateString(), "|", d.toTimeString());
console.log(JSON.stringify({ at: d }), String(d) === d.toString(), `${d}` === d.toString());
console.log(Date.UTC(2024, 1, 29), Date.UTC(2024, 1, 30), Date.UTC(99, 0), Date.UTC(1969, 11, 31, 23, 59, 59, 999));
const p = new Date(2024, 0, 31, 13, 5, 9, 7);
console.log(p.toISOString(), new Date(2024, 13, 1).toISOString());
for (const s of ["2023-11-14T22:13:20Z", "2023-11-14T22:13:20.123+08:00", "2023-11-14", "2023-11", "2023",
                 "2023-02-29", "2023-11-14T25:00:00Z", "garbage", "Tue, 14 Nov 2023 22:13:20 GMT", "+002023-01-01T00:00:00Z"]) {
  console.log(s, Date.parse(s));
}
const inv = new Date("nope");
console.log(inv.getTime(), String(inv), inv.toUTCString(), JSON.stringify({ x: inv }));
const a = new Date(1000), b = new Date(2000);
console.log(a < b, a > b, a <= new Date(1000), a === new Date(1000));
const c = new Date(a);
c.setTime(5000);
console.log(a.getTime(), c.getTime());
const early = new Date(-62198755200000);
console.log(early.toISOString(), new Date(8.64e15).toISOString(), new Date(8.64e15 + 1).getTime());
const ts = Math.floor(new Date(Date.UTC(2023, 10, 14)).getTime() / 1000);
console.log(ts, new Date(ts * 1000).toISOString().slice(0, 10).replaceAll("-", ""));
console.log(d);
