// @feature: `Date`
// @category: JavaScript: Standard Library
// @status: partial
// @note: UTC semantics (local time is UTC); constructors, `Date.UTC` / `parse` / `now`, getters, `setTime`, string conversions
const d = new Date(Date.UTC(2024, 1, 29, 12, 30, 15, 250));
console.log(d.toISOString(), d.getTime(), d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), d.getUTCDay(), d.getUTCHours());
console.log(d.toUTCString(), String(d), d.toDateString(), JSON.stringify({ d }), d.getTimezoneOffset());
const e = new Date("2024-03-01T00:00:00Z");
console.log((e.getTime() - d.getTime()) / 36e5, e > d, Date.parse("2000-01-01"), new Date(2020, 0, 31).toISOString(), typeof Date.now());
e.setTime(0);
console.log(e.toISOString(), new Date(NaN).getTime());
