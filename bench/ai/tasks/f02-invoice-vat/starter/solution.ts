// Totals for the shop's invoices. Prices come in euros from the catalogue; every amount computed here is in
// integer cents.
import { tryParse } from "e7:json";

interface InvoiceLine {
  sku: string;
  unitPrice: number; // euros, e.g. 0.99
  qty: number;
  vatRate: number; // percent
  discountPercent?: number;
}

interface Invoice {
  lines: InvoiceLine[];
}

interface LineTotal {
  sku: string;
  net: number;
}

interface VatGroup {
  rate: number;
  net: number;
  vat: number;
}

interface Totals {
  lines: LineTotal[];
  vatGroups: VatGroup[];
  net: number;
  vat: number;
  gross: number;
}

interface ErrorResult {
  error: string;
}

const VAT_RATES = [0, 2.1, 5.5, 10, 20];

function error(message: string): string {
  const e: ErrorResult = { error: message };
  return JSON.stringify(e);
}

function toCents(euros: number): number {
  return Math.round(euros * 100);
}

function lineNet(line: InvoiceLine): number {
  const discount = line.discountPercent ?? 0;
  return Math.round((toCents(line.unitPrice) * line.qty * (100 - discount)) / 100);
}

export function invoiceTotals(invoiceJson: string): string {
  const invoice = tryParse(invoiceJson) as Invoice | null;
  if (invoice === null) return error("invalid invoice");

  const lines: LineTotal[] = [];
  const groups = new Map<number, VatGroup>();
  for (let i = 0; i < invoice.lines.length; i++) {
    const line = invoice.lines[i];
    if (!Number.isInteger(line.qty) || line.qty <= 0) return error(`bad quantity on line ${i + 1}`);
    if (!VAT_RATES.includes(line.vatRate)) return error(`bad VAT rate on line ${i + 1}`);
    const discount = line.discountPercent ?? 0;
    if (discount < 0 || discount > 100) return error(`bad discount on line ${i + 1}`);

    const net = lineNet(line);
    lines.push({ sku: line.sku, net });

    let group = groups.get(line.vatRate);
    if (group === undefined) {
      group = { rate: line.vatRate, net: 0, vat: 0 };
      groups.set(line.vatRate, group);
    }
    group.net += net;
    group.vat += Math.round((net * line.vatRate) / 100);
  }

  const vatGroups = [...groups.values()].sort((a, b) => a.rate - b.rate);
  let net = 0;
  let vat = 0;
  for (const g of vatGroups) {
    net += g.net;
    vat += g.vat;
  }
  const totals: Totals = { lines, vatGroups, net, vat, gross: net + vat };
  return JSON.stringify(totals);
}
