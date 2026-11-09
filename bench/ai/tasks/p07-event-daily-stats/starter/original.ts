// Aggregates the raw event log (one event per line) into per-day rows for the dashboard. Days are UTC.
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

export interface DayRow {
  day: string;
  views: number;
  clicks: number;
  purchases: number;
  refunds: number;
  users: number;
  revenue: string;
  ctr: string;
}

const LINE_RE = /^(\S+)\s+(view|click|purchase|refund)\s+user=(\w+)(?:\s+amount=(\d+(?:\.\d{1,2})?))?\s*$/;
const DAY_MS = 24 * 60 * 60 * 1000;

function parseLine(line: string): LogEvent | null {
  const m = LINE_RE.exec(line);
  if (!m) return null;
  const time = new Date(m[1]);
  if (isNaN(time.getTime())) return null;
  return { time, type: m[2] as EventType, userId: m[3], amountCents: m[4] ? Math.round(parseFloat(m[4]) * 100) : 0 };
}

class DayStats {
  counts: Record<EventType, number> = { view: 0, click: 0, purchase: 0, refund: 0 };
  users = new Set<string>();
  revenueCents = 0;

  get clickThrough(): number | null {
    return this.counts.view === 0 ? null : this.counts.click / this.counts.view;
  }

  add(e: LogEvent): void {
    this.counts[e.type]++;
    this.users.add(e.userId);
    if (e.type === "purchase") this.revenueCents += e.amountCents;
    if (e.type === "refund") this.revenueCents -= e.amountCents;
  }
}

class Aggregator {
  private days: { [day: string]: DayStats } = {};
  skipped: number[] = [];

  addLog(log: string): void {
    log.split("\n").forEach((line, i) => {
      if (/^\s*(#.*)?$/.test(line)) return; // blank lines and comments
      const event = parseLine(line);
      if (!event) {
        this.skipped.push(i + 1);
        return;
      }
      const day = event.time.toISOString().slice(0, 10);
      (this.days[day] ??= new DayStats()).add(event);
    });
  }

  // keeps the days in [from, to]; an empty bound is open
  restrict(from: string, to: string): void {
    for (const day in this.days) {
      if ((from && day < from) || (to && day > to)) delete this.days[day];
    }
  }

  // every day from the first to the last one with events, empty days included
  rows(): DayRow[] {
    const keys = Object.keys(this.days).sort();
    if (keys.length === 0) return [];
    const out: [string, DayStats][] = [];
    const last = Date.parse(keys[keys.length - 1]);
    for (let t = Date.parse(keys[0]); t <= last; t += DAY_MS) {
      const day = new Date(t).toISOString().slice(0, 10);
      out.push([day, this.days[day] ?? new DayStats()]);
    }
    return out.map(Aggregator.toRow);
  }

  static toRow([day, s]: [string, DayStats]): DayRow {
    const ctr = s.clickThrough;
    return {
      day,
      views: s.counts.view,
      clicks: s.counts.click,
      purchases: s.counts.purchase,
      refunds: s.counts.refund,
      users: s.users.size,
      revenue: (s.revenueCents / 100).toFixed(2),
      ctr: ctr === null ? "-" : `${(ctr * 100).toFixed(1)}%`,
    };
  }
}

// Called by the app: the log text and an optional day range ("YYYY-MM-DD", "" for open).
export function dailyStats(log: string, fromDay: string, toDay: string): string {
  const agg = new Aggregator();
  agg.addLog(log);
  agg.restrict(fromDay, toDay);
  return JSON.stringify({ days: agg.rows(), skippedLines: agg.skipped });
}
