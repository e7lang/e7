// E7 port of original.ts: aggregates the raw event log (one event per line) into per-day rows. Days are UTC.
//
//   2024-03-01T10:00:00Z view user=u17
//   2024-03-01T10:02:13Z purchase user=u17 amount=19.99

type EventType = "view" | "click" | "purchase" | "refund";

interface LogEvent {
  time: Date;
  type: EventType;
  userId: string;
  amountCents: number;
}

interface DayRow {
  day: string;
  views: number;
  clicks: number;
  purchases: number;
  refunds: number;
  users: number;
  revenue: string;
  ctr: string;
}

const DAY_MS = 24 * 60 * 60 * 1000;

// \s of the original's regular expressions
function isSpace(c: string): boolean {
  return c === " " || c === "\t" || c === "\r" || c === "\n" || c === "\v" || c === "\f" || c === " " || c === "﻿";
}
const isDigit = (c: string) => c >= "0" && c <= "9";
const isWordChar = (c: string) => isDigit(c) || (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c === "_";
const allDigits = (s: string) => s.length > 0 && [...s].every((c) => isDigit(c));

function tokens(line: string): string[] {
  const out: string[] = [];
  let cur = "";
  for (const c of line) {
    if (isSpace(c)) {
      if (cur !== "") out.push(cur);
      cur = "";
    } else {
      cur += c;
    }
  }
  if (cur !== "") out.push(cur);
  return out;
}

function eventType(s: string): EventType | null {
  switch (s) {
    case "view":
      return "view";
    case "click":
      return "click";
    case "purchase":
      return "purchase";
    case "refund":
      return "refund";
  }
  return null;
}

// \d+(\.\d{1,2})?
function isAmount(s: string): boolean {
  const dot = s.indexOf(".");
  if (dot < 0) return allDigits(s);
  const frac = s.slice(dot + 1);
  return allDigits(s.slice(0, dot)) && frac.length <= 2 && allDigits(frac);
}

// <time> <type> user=<word> [amount=<number>], separated by whitespace
function parseLine(line: string): LogEvent | null {
  if (line.length === 0 || isSpace(line[0])) return null;
  const t = tokens(line);
  if (t.length < 3 || t.length > 4) return null;
  const type = eventType(t[1]);
  if (type === null) return null;
  if (!t[2].startsWith("user=")) return null;
  const userId = t[2].slice(5);
  if (userId.length === 0 || ![...userId].every((c) => isWordChar(c))) return null;
  let amountCents = 0;
  if (t.length === 4) {
    if (!t[3].startsWith("amount=") || !isAmount(t[3].slice(7))) return null;
    amountCents = Math.round(parseFloat(t[3].slice(7)) * 100);
  }
  const time = new Date(t[0]);
  if (isNaN(time.getTime())) return null;
  return { time, type, userId, amountCents };
}

function isBlankOrComment(line: string): boolean {
  const s = line.trimStart();
  return s === "" || (s.startsWith("#") && !s.includes("\n") && !s.includes("\r"));
}

class DayStats {
  views = 0;
  clicks = 0;
  purchases = 0;
  refunds = 0;
  users = new Set<string>();
  revenueCents = 0;

  get clickThrough(): number | null {
    return this.views === 0 ? null : this.clicks / this.views;
  }

  add(e: LogEvent): void {
    switch (e.type) {
      case "view":
        this.views++;
        break;
      case "click":
        this.clicks++;
        break;
      case "purchase":
        this.purchases++;
        this.revenueCents += e.amountCents;
        break;
      case "refund":
        this.refunds++;
        this.revenueCents -= e.amountCents;
        break;
    }
    this.users.add(e.userId);
  }
}

function toRow(day: string, s: DayStats): DayRow {
  const ctr = s.clickThrough;
  return {
    day,
    views: s.views,
    clicks: s.clicks,
    purchases: s.purchases,
    refunds: s.refunds,
    users: s.users.size,
    revenue: (s.revenueCents / 100).toFixed(2),
    ctr: ctr === null ? "-" : `${(ctr * 100).toFixed(1)}%`,
  };
}

class Aggregator {
  private days = new Map<string, DayStats>();
  skipped: number[] = [];

  addLog(log: string): void {
    const lines = log.split("\n");
    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      if (isBlankOrComment(line)) continue;
      const event = parseLine(line);
      if (event === null) {
        this.skipped.push(i + 1);
        continue;
      }
      const day = event.time.toISOString().slice(0, 10);
      let stats = this.days.get(day);
      if (stats === undefined) {
        stats = new DayStats();
        this.days.set(day, stats);
      }
      stats.add(event);
    }
  }

  // keeps the days in [from, to]; an empty bound is open
  restrict(from: string, to: string): void {
    for (const day of [...this.days.keys()]) {
      if ((from !== "" && day < from) || (to !== "" && day > to)) this.days.delete(day);
    }
  }

  // every day from the first to the last one with events, empty days included
  rows(): DayRow[] {
    const keys = [...this.days.keys()].sort();
    if (keys.length === 0) return [];
    const out: DayRow[] = [];
    const last = Date.parse(keys[keys.length - 1]);
    for (let t = Date.parse(keys[0]); t <= last; t += DAY_MS) {
      const day = new Date(t).toISOString().slice(0, 10);
      out.push(toRow(day, this.days.get(day) ?? new DayStats()));
    }
    return out;
  }
}

interface Result {
  days: DayRow[];
  skippedLines: number[];
}

// Called by the app: the log text and an optional day range ("YYYY-MM-DD", "" for open).
export function dailyStats(log: string, fromDay: string, toDay: string): string {
  const agg = new Aggregator();
  agg.addLog(log);
  agg.restrict(fromDay, toDay);
  const r: Result = { days: agg.rows(), skippedLines: agg.skipped };
  return JSON.stringify(r);
}
