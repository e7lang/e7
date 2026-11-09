export interface LicenseCheck { ok: boolean; error: string; normalized: string; edition: string }

const ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

const EDITIONS = new Map<string, string>([
  ["S", "standard"],
  ["P", "pro"],
  ["E", "enterprise"],
]);

function checkChar(body: string): string {
  let sum = 0;
  for (let i = 0; i < body.length; i++) sum += (i + 1) * ALPHABET.indexOf(body[i]);
  return ALPHABET[sum % 32];
}

function format(key: string): string {
  const groups: string[] = [];
  for (let i = 0; i < key.length; i += 5) groups.push(key.slice(i, i + 5));
  return groups.join("-");
}

function fail(error: string): LicenseCheck {
  return { ok: false, error, normalized: "", edition: "" };
}

export function checkLicenseKey(input: string): LicenseCheck {
  const key = input.replaceAll("-", "").replaceAll(" ", "").toUpperCase();
  if (key.length !== 20) return fail("wrong length");
  for (let i = 0; i < key.length; i++) {
    if (!ALPHABET.includes(key[i])) return fail(`invalid character "${key[i]}" at position ${i + 1}`);
  }
  if (checkChar(key.slice(0, 19)) !== key[19]) return fail("bad checksum");
  const edition = EDITIONS.get(key[0]);
  if (edition === undefined) return fail("unknown edition");
  return { ok: true, error: "", normalized: format(key), edition };
}

export function makeLicenseKey(body: string): string {
  if (body.length !== 19) throw new Error("invalid key body");
  for (const c of body) {
    if (!ALPHABET.includes(c)) throw new Error("invalid key body");
  }
  return format(body + checkChar(body));
}
