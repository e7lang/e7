// Renders the plain-text invoice that is printed and attached to the receipt e-mail.
// VAT is computed per category on the sum of the line nets (rounded once per category).

export enum TaxCategory {
  Standard = "standard",
  Reduced = "reduced",
  Zero = "zero",
  Exempt = "exempt",
}

type RateTable = Record<Exclude<TaxCategory, TaxCategory.Exempt>, number>;

const VAT_RATES: { [country: string]: RateTable } = {
  DE: { standard: 19, reduced: 7, zero: 0 },
  FR: { standard: 20, reduced: 5.5, zero: 0 },
  NL: { standard: 21, reduced: 9, zero: 0 },
};

export interface LineInput {
  description: string;
  qty: number;
  unitPrice: number;
  category: TaxCategory;
  discountPct: number;
}

export interface InvoiceInput {
  number: string;
  country: string;
  currency: string;
  lines: Partial<LineInput>[];
}

const LINE_DEFAULTS: Partial<LineInput> = { qty: 1, discountPct: 0, category: TaxCategory.Standard };

const round2 = (x: number) => Math.round(x * 100) / 100;
const money = (x: number) => x.toFixed(2);

interface Priced {
  readonly net: number;
  readonly exempt: boolean;
}

class InvoiceLine implements Priced {
  constructor(readonly input: LineInput) {}
  get net(): number {
    const { qty, unitPrice, discountPct } = this.input;
    return round2(qty * unitPrice * (1 - discountPct / 100));
  }
  get exempt(): boolean {
    return this.input.category === TaxCategory.Exempt;
  }
}

function toLine(raw: Partial<LineInput>, index: number, rates: RateTable): InvoiceLine {
  const input = { ...LINE_DEFAULTS, ...raw } as LineInput;
  const where = `line ${index + 1}`;
  if (!input.description) throw new Error(`${where}: missing description`);
  if (!(input.qty > 0)) throw new Error(`${where}: quantity must be positive`);
  if (input.discountPct < 0 || input.discountPct > 100) throw new Error(`${where}: discount must be 0-100`);
  if (input.category !== TaxCategory.Exempt && !(input.category in rates)) {
    throw new Error(`${where}: unknown tax category "${input.category}"`);
  }
  return new InvoiceLine(input);
}

class InvoiceFormatter {
  constructor(private rates: RateTable) {}

  rateLabel(line: InvoiceLine): string {
    return line.exempt ? "exempt*" : `${this.rates[line.input.category as keyof RateTable]}%`;
  }

  formatLine(line: InvoiceLine, label: string): string {
    const desc = line.input.description.length > 24 ? line.input.description.slice(0, 21) + "..." : line.input.description;
    const qty = `${line.input.qty} x`.padStart(6);
    const off = line.input.discountPct ? ` -${line.input.discountPct}%` : "";
    return `${qty} ${desc.padEnd(24)} @ ${money(line.input.unitPrice).padStart(8)}${off} = ${money(line.net).padStart(9)}  ${label}`;
  }

  render(invoice: InvoiceInput, lines: InvoiceLine[]): string {
    const labels = lines.map((l) => this.rateLabel(l));
    const out = [`Invoice ${invoice.number} (${invoice.country}, ${invoice.currency})`];
    lines.forEach((line, i) => out.push(this.formatLine(line, labels[i])));

    const netByCategory: Partial<Record<TaxCategory, number>> = {};
    for (const line of lines) {
      const cat = line.input.category;
      netByCategory[cat] = round2((netByCategory[cat] ?? 0) + line.net);
    }
    const net = round2(lines.map((l) => l.net).reduce((a, b) => a + b, 0));
    out.push(`${"Net".padEnd(32)}${money(net).padStart(12)}`);

    let total = net;
    for (const [cat, base] of Object.entries(netByCategory)) {
      if (cat === TaxCategory.Exempt) continue;
      const rate = this.rates[cat as keyof RateTable];
      const vat = round2((base * rate) / 100);
      total = round2(total + vat);
      out.push(`${`VAT ${rate}% on ${money(base)}`.padEnd(32)}${money(vat).padStart(12)}`);
    }
    out.push(`${"Total " + invoice.currency}`.padEnd(32) + money(total).padStart(12));
    if (lines.some((l) => l.exempt)) out.push("* exempt from VAT");
    return out.join("\n");
  }
}

// Called by the app with the invoice as JSON; returns the invoice text or "error: <reason>".
export function renderInvoice(invoiceJson: string): string {
  try {
    const invoice: InvoiceInput = JSON.parse(invoiceJson);
    if (!/^INV-\d{4}-\d{4,}$/.test(invoice.number)) throw new Error(`invalid invoice number "${invoice.number}"`);
    const rates = VAT_RATES[invoice.country.toUpperCase()];
    if (!rates) throw new Error(`no VAT rates for ${invoice.country}`);
    const lines = invoice.lines.map((raw, i) => toLine(raw, i, rates));
    return new InvoiceFormatter(rates).render(invoice, lines);
  } catch (e) {
    return e instanceof SyntaxError ? "error: invalid json" : `error: ${(e as Error).message}`;
  }
}
