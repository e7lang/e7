// E7 port of original.ts: renders the plain-text invoice. VAT is computed per category on the sum of the
// line nets (rounded once per category).

// The tax category stays a string so that an unknown category gets its own error message.
const EXEMPT = "exempt";

interface RateTable {
  standard: number;
  reduced: number;
  zero: number;
}

const VAT_RATES = new Map<string, RateTable>([
  ["DE", { standard: 19, reduced: 7, zero: 0 }],
  ["FR", { standard: 20, reduced: 5.5, zero: 0 }],
  ["NL", { standard: 21, reduced: 9, zero: 0 }],
]);

function rateOf(rates: RateTable, category: string): number | null {
  switch (category) {
    case "standard":
      return rates.standard;
    case "reduced":
      return rates.reduced;
    case "zero":
      return rates.zero;
  }
  return null;
}

// a line as it arrives: everything but unitPrice may be left out
interface RawLine {
  description?: string;
  qty?: number;
  unitPrice: number;
  category?: string;
  discountPct?: number;
}

interface InvoiceInput {
  number: string;
  country: string;
  currency: string;
  lines: RawLine[];
}

interface LineInput {
  description: string;
  qty: number;
  unitPrice: number;
  category: string;
  discountPct: number;
}

const round2 = (x: number) => Math.round(x * 100) / 100;
const money = (x: number) => x.toFixed(2);

class InvoiceLine {
  constructor(readonly input: LineInput) {}
  get net(): number {
    const { qty, unitPrice, discountPct } = this.input;
    return round2(qty * unitPrice * (1 - discountPct / 100));
  }
  get exempt(): boolean {
    return this.input.category === EXEMPT;
  }
}

function toLine(raw: RawLine, index: number, rates: RateTable): InvoiceLine {
  const input: LineInput = {
    description: raw.description ?? "",
    qty: raw.qty ?? 1,
    unitPrice: raw.unitPrice,
    category: raw.category ?? "standard",
    discountPct: raw.discountPct ?? 0,
  };
  const where = `line ${index + 1}`;
  if (!input.description) throw new Error(`${where}: missing description`);
  if (!(input.qty > 0)) throw new Error(`${where}: quantity must be positive`);
  if (input.discountPct < 0 || input.discountPct > 100) throw new Error(`${where}: discount must be 0-100`);
  if (input.category !== EXEMPT && rateOf(rates, input.category) === null) {
    throw new Error(`${where}: unknown tax category "${input.category}"`);
  }
  return new InvoiceLine(input);
}

class InvoiceFormatter {
  constructor(private rates: RateTable) {}

  rateLabel(line: InvoiceLine): string {
    return line.exempt ? "exempt*" : `${rateOf(this.rates, line.input.category)}%`;
  }

  formatLine(line: InvoiceLine, label: string): string {
    const d = line.input.description;
    const desc = d.length > 24 ? d.slice(0, 21) + "..." : d;
    const qty = `${line.input.qty} x`.padStart(6);
    const off = line.input.discountPct ? ` -${line.input.discountPct}%` : "";
    return `${qty} ${desc.padEnd(24)} @ ${money(line.input.unitPrice).padStart(8)}${off} = ${money(line.net).padStart(9)}  ${label}`;
  }

  render(invoice: InvoiceInput, lines: InvoiceLine[]): string {
    const out = [`Invoice ${invoice.number} (${invoice.country}, ${invoice.currency})`];
    for (const line of lines) out.push(this.formatLine(line, this.rateLabel(line)));

    // categories in order of first use
    const netByCategory = new Map<string, number>();
    for (const line of lines) {
      const cat = line.input.category;
      netByCategory.set(cat, round2((netByCategory.get(cat) ?? 0) + line.net));
    }
    const net = round2(lines.map((l) => l.net).reduce((a, b) => a + b, 0));
    out.push(`${"Net".padEnd(32)}${money(net).padStart(12)}`);

    let total = net;
    for (const [cat, base] of netByCategory) {
      const rate = rateOf(this.rates, cat);
      if (rate === null) continue; // exempt
      const vat = round2((base * rate) / 100);
      total = round2(total + vat);
      out.push(`${`VAT ${rate}% on ${money(base)}`.padEnd(32)}${money(vat).padStart(12)}`);
    }
    out.push(`Total ${invoice.currency}`.padEnd(32) + money(total).padStart(12));
    if (lines.some((l) => l.exempt)) out.push("* exempt from VAT");
    return out.join("\n");
  }
}

const isDigits = (s: string) => s.length > 0 && [...s].every((c) => c >= "0" && c <= "9");

// /^INV-\d{4}-\d{4,}$/
function validNumber(n: string): boolean {
  if (!n.startsWith("INV-")) return false;
  const parts = n.slice(4).split("-");
  return parts.length === 2 && parts[0].length === 4 && isDigits(parts[0]) && parts[1].length >= 4 && isDigits(parts[1]);
}

function render(invoice: InvoiceInput): string {
  if (!validNumber(invoice.number)) throw new Error(`invalid invoice number "${invoice.number}"`);
  const rates = VAT_RATES.get(invoice.country.toUpperCase());
  if (rates === undefined) throw new Error(`no VAT rates for ${invoice.country}`);
  const lines = invoice.lines.map((raw, i) => toLine(raw, i, rates));
  return new InvoiceFormatter(rates).render(invoice, lines);
}

// Called by the app with the invoice as JSON; returns the invoice text or "error: <reason>".
export function renderInvoice(invoiceJson: string): string {
  let invoice: InvoiceInput;
  try {
    invoice = JSON.parse(invoiceJson) as InvoiceInput;
  } catch (e) {
    return "error: invalid json";
  }
  try {
    return render(invoice);
  } catch (e) {
    return `error: ${e instanceof Error ? e.message : String(e)}`;
  }
}
