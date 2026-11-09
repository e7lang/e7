export interface SignupForm {
  username: string;
  email: string;
  password: string;
  confirmPassword: string;
  age: string;
  country: string;
  acceptTerms: boolean;
}
export interface FieldError { field: string; message: string }

const COUNTRIES = ["US", "CA", "GB", "DE", "FR", "JP"];

const isLetter = (c: string) => (c >= "a" && c <= "z") || (c >= "A" && c <= "Z");
const isDigit = (c: string) => c >= "0" && c <= "9";

function checkUsername(name: string): string | null {
  if (name === "") return "is required";
  if (name.length < 3 || name.length > 20) return "must be 3 to 20 characters";
  for (const c of name) {
    if (!isLetter(c) && !isDigit(c) && c !== "_") return "may only contain letters, digits and _";
  }
  if (!isLetter(name[0])) return "must start with a letter";
  return null;
}

function checkEmail(email: string): string | null {
  if (email === "") return "is required";
  const parts = email.split("@");
  if (parts.length !== 2 || email.includes(" ")) return "is not a valid email address";
  const [local, domain] = parts;
  const dot = domain.indexOf(".");
  if (local === "" || dot <= 0 || domain.endsWith(".")) return "is not a valid email address";
  return null;
}

function checkPassword(password: string, username: string): string | null {
  if (password === "") return "is required";
  if (password.length < 8) return "must be at least 8 characters";
  const chars = [...password];
  if (!chars.some(isLetter) || !chars.some(isDigit)) return "must contain a letter and a digit";
  if (username !== "" && password.toLowerCase().includes(username.toLowerCase())) return "must not contain the username";
  return null;
}

function checkAge(age: string): string | null {
  if (age === "") return null;
  if (![...age].every(isDigit)) return "must be a whole number";
  const n = Number(age);
  if (n < 13 || n > 120) return "must be between 13 and 120";
  return null;
}

export function validateSignup(form: SignupForm): FieldError[] {
  const errors: FieldError[] = [];
  const add = (field: string, message: string | null) => {
    if (message !== null) errors.push({ field, message });
  };
  const username = form.username.trim();
  add("username", checkUsername(username));
  add("email", checkEmail(form.email.trim()));
  add("password", checkPassword(form.password, username));
  if (form.password !== "" && form.confirmPassword !== form.password) add("confirmPassword", "does not match");
  add("age", checkAge(form.age.trim()));
  const country = form.country.trim().toUpperCase();
  if (country === "") add("country", "is required");
  else if (!COUNTRIES.includes(country)) add("country", "is not supported");
  if (!form.acceptTerms) add("acceptTerms", "must be accepted");
  return errors;
}
