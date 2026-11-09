// Normalizes and validates the "edit profile" form before the profile is saved.
// The form posts every field as a string; older clients still send the short field names.

export interface Profile {
  displayName: string;
  email: string;
  phone?: string;
  age?: number;
  website?: string;
  tags: string[];
}

type FormValues = Record<string, string>;
type FieldErrors = Partial<Record<keyof Profile, string>>;

const KNOWN_FIELDS: (keyof Profile)[] = ["displayName", "email", "phone", "age", "website", "tags"];
const ALIASES: { [alias: string]: keyof Profile } = { name: "displayName", mail: "email", tel: "phone", url: "website" };
const REQUIRED: (keyof Profile)[] = ["displayName", "email"];

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const MAX_TAGS = 5;

const collapseSpaces = (s: string) => s.trim().replace(/\s+/g, " ");

function normalizeName(raw: string): string {
  const name = collapseSpaces(raw);
  if (name.length < 2 || name.length > 40) throw new Error("must be 2-40 characters");
  return name;
}

function normalizeEmail(raw: string): string {
  const email = raw.trim().toLowerCase();
  if (!EMAIL_RE.test(email)) throw new Error("not a valid email address");
  return email;
}

function normalizePhone(raw: string): string {
  let digits = raw.replace(/\D/g, "");
  if (digits.length === 11 && digits.startsWith("1")) digits = digits.slice(1);
  if (digits.length !== 10) throw new Error("must have 10 digits");
  return `(${digits.slice(0, 3)}) ${digits.slice(3, 6)}-${digits.slice(6)}`;
}

function normalizeAge(raw: string): number {
  if (!/^\d+$/.test(raw.trim())) throw new Error("must be a whole number");
  const age = parseInt(raw, 10);
  if (age < 13 || age > 120) throw new Error("must be between 13 and 120");
  return age;
}

function normalizeWebsite(raw: string): string {
  const url = raw.trim().replace(/^https?:\/\//i, "").replace(/\/+$/, "");
  const slash = url.indexOf("/");
  const host = slash < 0 ? url : url.slice(0, slash);
  if (/\s/.test(url) || !host.includes(".")) throw new Error("not a valid URL");
  return "https://" + host.toLowerCase() + (slash < 0 ? "" : url.slice(slash));
}

function normalizeTags(raw: string): string[] {
  const tags = raw.split(/[,;]/).map((t) => collapseSpaces(t).toLowerCase()).filter((t) => t.length > 0);
  const unique = [...new Set(tags)];
  if (unique.length > MAX_TAGS) throw new Error(`at most ${MAX_TAGS} tags`);
  return unique;
}

export interface NormalizeResult {
  profile?: Profile;
  errors: FieldErrors;
  ignored: string[];
}

export function normalizeProfile(form: FormValues): NormalizeResult {
  const values: FormValues = {};
  const ignored: string[] = [];
  for (const key in form) {
    const field: string = key in ALIASES ? ALIASES[key] : key;
    if (!(KNOWN_FIELDS as string[]).includes(field)) {
      ignored.push(key);
      continue;
    }
    values[field] = form[key];
  }
  // a field left blank counts as not given
  for (const key in values) {
    if (values[key].trim() === "") delete values[key];
  }

  const errors: FieldErrors = {};
  const draft: Partial<Profile> = {};
  function run<K extends keyof Profile>(field: K, normalize: (raw: string) => Profile[K]): void {
    if (!(field in values)) {
      if (REQUIRED.includes(field)) errors[field] = "is required";
      return;
    }
    try {
      draft[field] = normalize(values[field]);
    } catch (e) {
      errors[field] = (e as Error).message;
    }
  }
  run("displayName", normalizeName);
  run("email", normalizeEmail);
  run("phone", normalizePhone);
  run("age", normalizeAge);
  run("website", normalizeWebsite);
  run("tags", normalizeTags);

  if (Object.keys(errors).length > 0) return { errors, ignored };
  const profile: Profile = {
    displayName: draft.displayName!,
    email: draft.email!,
    phone: draft.phone,
    age: draft.age,
    website: draft.website,
    tags: draft.tags ?? [],
  };
  return { profile, errors, ignored };
}

// Called by the app with the form as a JSON object of strings.
export function submitProfileForm(formJson: string): string {
  let form: FormValues;
  try {
    form = JSON.parse(formJson);
  } catch {
    return JSON.stringify({ ok: false, errors: ["form: invalid json"] });
  }
  const { profile, errors, ignored } = normalizeProfile(form);
  if (!profile) {
    return JSON.stringify({ ok: false, errors: Object.entries(errors).map(([field, msg]) => `${field}: ${msg}`) });
  }
  return JSON.stringify({ ok: true, profile, ignored });
}
