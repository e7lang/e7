// Billing schedule of subscriptions. Dates are calendar dates ("YYYY-MM-DD") in UTC.
//
// A subscription renews every `periodMonths` months on the day of the month it started. When a month is too
// short for that day, it renews on the month's last day instead.

const DAY_MS = 24 * 60 * 60 * 1000;

function isDigits(s: string): boolean {
  if (s.length === 0) return false;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 48 || c > 57) return false;
  }
  return true;
}

function parseDate(text: string): Date {
  const parts = text.split("-");
  if (parts.length !== 3 || parts[0].length !== 4 || parts[1].length !== 2 || parts[2].length !== 2 ||
      !parts.every((p) => isDigits(p))) {
    throw new Error(`invalid date ${text}`);
  }
  const year = Number(parts[0]);
  const month = Number(parts[1]) - 1;
  const day = Number(parts[2]);
  const d = new Date(Date.UTC(year, month, day));
  if (d.getUTCFullYear() !== year || d.getUTCMonth() !== month || d.getUTCDate() !== day) {
    throw new Error(`invalid date ${text}`);
  }
  return d;
}

function formatDate(d: Date): string {
  return d.toISOString().slice(0, 10);
}

function daysInMonth(year: number, month: number): number {
  return new Date(Date.UTC(year, month + 1, 0)).getUTCDate();
}

// The same day of the month, `months` months later, clamped to the last day of the target month.
function addMonths(d: Date, months: number): Date {
  const first = new Date(Date.UTC(d.getUTCFullYear(), d.getUTCMonth() + months, 1));
  const year = first.getUTCFullYear();
  const month = first.getUTCMonth();
  const day = Math.min(d.getUTCDate(), daysInMonth(year, month));
  return new Date(Date.UTC(year, month, day));
}

// Every renewal date after `start`, in order.
function* renewals(start: Date, periodMonths: number): Generator<Date> {
  let d = start;
  for (;;) {
    d = addMonths(d, periodMonths);
    yield d;
  }
}

function checkPeriod(periodMonths: number): void {
  if (!Number.isInteger(periodMonths) || periodMonths <= 0) throw new Error(`invalid period ${periodMonths}`);
}

// The first `count` renewal dates after `start`.
export function renewalDates(start: string, periodMonths: number, count: number): string[] {
  checkPeriod(periodMonths);
  const result: string[] = [];
  if (count <= 0) return result;
  for (const d of renewals(parseDate(start), periodMonths)) {
    result.push(formatDate(d));
    if (result.length === count) break;
  }
  return result;
}

// Refund in cents when the subscription is cancelled on `cancelDate`: the price of the current billing period,
// prorated by the days from `cancelDate` to the next renewal date.
export function refundCents(priceCents: number, start: string, periodMonths: number, cancelDate: string): number {
  checkPeriod(periodMonths);
  const first = parseDate(start);
  const cancel = parseDate(cancelDate).getTime();
  if (cancel < first.getTime()) throw new Error("cancel date before start");
  let periodStart = first.getTime();
  for (const d of renewals(first, periodMonths)) {
    const t = d.getTime();
    if (t > cancel) {
      const unusedDays = (t - cancel) / DAY_MS;
      const periodDays = (t - periodStart) / DAY_MS;
      return Math.round((priceCents * unusedDays) / periodDays);
    }
    periodStart = t;
  }
  return 0;
}
