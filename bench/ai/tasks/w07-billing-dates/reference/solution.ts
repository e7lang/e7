export interface Period { start: string; end: string }

const DAY_MS = 86400000;

function isDigits(s: string): boolean {
  return s.length > 0 && [...s].every((c) => c >= "0" && c <= "9");
}

// Parses YYYY-MM-DD into a UTC Date, rejecting impossible dates.
function parseDate(text: string): Date {
  const parts = text.split("-");
  if (parts.length !== 3 || parts[0].length !== 4 || parts[1].length !== 2 || parts[2].length !== 2 || !parts.every(isDigits)) {
    throw new Error(`invalid date: ${text}`);
  }
  const [y, m, d] = parts.map((p) => Number(p));
  const date = new Date(Date.UTC(y, m - 1, d));
  if (date.getUTCFullYear() !== y || date.getUTCMonth() !== m - 1 || date.getUTCDate() !== d) {
    throw new Error(`invalid date: ${text}`);
  }
  return date;
}

function format(date: Date): string {
  return date.toISOString().slice(0, 10);
}

function addDays(date: Date, n: number): Date {
  return new Date(date.getTime() + n * DAY_MS);
}

function isBusinessDay(date: Date, holidays: Set<string>): boolean {
  const wd = date.getUTCDay();
  return wd !== 0 && wd !== 6 && !holidays.has(format(date));
}

export function addBusinessDays(date: string, days: number, holidays: string[]): string {
  const off = new Set<string>(holidays);
  let d = parseDate(date);
  const step = days < 0 ? -1 : 1;
  if (days === 0) {
    while (!isBusinessDay(d, off)) d = addDays(d, 1);
    return format(d);
  }
  let left = Math.abs(days);
  while (left > 0) {
    d = addDays(d, step);
    if (isBusinessDay(d, off)) left--;
  }
  return format(d);
}

function anchorPlusMonths(anchor: Date, months: number): Date {
  const y = anchor.getUTCFullYear();
  const m = anchor.getUTCMonth() + months;
  const lastDay = new Date(Date.UTC(y, m + 1, 0)).getUTCDate();
  return new Date(Date.UTC(y, m, Math.min(anchor.getUTCDate(), lastDay)));
}

export function billingDate(anchor: string, cycle: number): string {
  return format(anchorPlusMonths(parseDate(anchor), cycle));
}

export function billingPeriod(anchor: string, cycle: number): Period {
  const a = parseDate(anchor);
  return { start: format(anchorPlusMonths(a, cycle)), end: format(addDays(anchorPlusMonths(a, cycle + 1), -1)) };
}

export function proratedCents(priceCents: number, period: Period, fromDate: string): number {
  const start = parseDate(period.start).getTime();
  const end = parseDate(period.end).getTime();
  const from = parseDate(fromDate).getTime();
  if (from < start || from > end) throw new Error(`date outside period: ${fromDate}`);
  const total = (end - start) / DAY_MS + 1;
  const remaining = (end - from) / DAY_MS + 1;
  return Math.round((priceCents * remaining) / total);
}
