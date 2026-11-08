// JSON.parse into literal types and enums (documents with valid values; invalid ones are rejected:
// see errors/rt_json_literal_*.ts)
import { tryParse } from "e7:json";
type Kind = "percent" | "tier";
enum Lv {
  Low = 1,
  High = 2,
}
enum Ev {
  Paid = "paid",
  Shipped = "shipped;x",
}
interface Rule {
  kind: Kind;
  value: number;
  lv?: Lv;
  ev: Ev | null;
  ok: true;
  n: 1 | 2.5;
}
const r = JSON.parse(`{"kind":"tier","value":5,"lv":2,"ev":"shipped;x","ok":true,"n":2.5}`) as Rule;
console.log(r.kind, r.value, r.lv, r.ev, r.ok, r.n, r.lv === Lv.High, r.ev === Ev.Shipped);
const t = tryParse(`{"kind":"percent","value":1,"ev":null,"ok":true,"n":1}`) as Rule | null;
console.log(t === null ? "null" : `${t.kind} ${t.lv} ${t.ev}`);
const xs = JSON.parse(`["percent","tier"]`) as Kind[];
console.log(xs.join(","));
