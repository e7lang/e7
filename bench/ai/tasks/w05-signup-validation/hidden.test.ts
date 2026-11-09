import { assertEqual } from "e7:test";
import { validateSignup } from "./solution.ts";
import type { SignupForm, FieldError } from "./solution.ts";

function valid(): SignupForm {
  return {
    username: "grace_h", email: "grace@navy.mil", password: "cobol1959", confirmPassword: "cobol1959",
    age: "", country: "US", acceptTerms: true,
  };
}

// errors for one field after changing the valid form
function errorsFor(field: string, form: SignupForm): string[] {
  return validateSignup(form).filter((e) => e.field === field).map((e) => e.message);
}

function username(v: string): string[] {
  const f = valid();
  f.username = v;
  return errorsFor("username", f);
}

function email(v: string): string[] {
  const f = valid();
  f.email = v;
  return errorsFor("email", f);
}

function password(v: string): FieldError[] {
  const f = valid();
  f.password = v;
  f.confirmPassword = v;
  return validateSignup(f);
}

function age(v: string): string[] {
  const f = valid();
  f.age = v;
  return errorsFor("age", f);
}

export function testValidFormVariants(): void {
  assertEqual(validateSignup(valid()), []);
  const f = valid();
  f.username = "  Grace_H  ";
  f.email = " grace@navy.mil ";
  f.country = " jp ";
  f.age = " 120 ";
  assertEqual(validateSignup(f), []);
}

export function testUsernameRules(): void {
  assertEqual(username(""), ["is required"]);
  assertEqual(username("   "), ["is required"]);
  assertEqual(username("ab"), ["must be 3 to 20 characters"]);
  assertEqual(username("abc"), []);
  assertEqual(username("a2345678901234567890"), []);
  assertEqual(username("a23456789012345678901"), ["must be 3 to 20 characters"]);
  assertEqual(username("bad name"), ["may only contain letters, digits and _"]);
  assertEqual(username("zoë"), ["may only contain letters, digits and _"]);
  assertEqual(username("_grace"), ["must start with a letter"]);
  assertEqual(username("1grace"), ["must start with a letter"]);
}

export function testUsernameRuleOrder(): void {
  // too short wins over bad characters and the first character
  assertEqual(username("1!"), ["must be 3 to 20 characters"]);
  // bad characters win over the first character
  assertEqual(username("1-grace"), ["may only contain letters, digits and _"]);
}

export function testEmailRules(): void {
  assertEqual(email(""), ["is required"]);
  assertEqual(email("a@b.co"), []);
  assertEqual(email("first.last@mail.example.org"), []);
  const bad = "is not a valid email address";
  for (const e of ["plain", "@b.co", "a@@b.co", "a@b@c.co", "a@bco", "a@.co", "a@b.", "a b@c.co", "a@b .co", "a@"]) {
    assertEqual(email(e), [bad], e);
  }
}

export function testPasswordRules(): void {
  assertEqual(password(""), [{ field: "password", message: "is required" }]);
  assertEqual(password("abc123"), [{ field: "password", message: "must be at least 8 characters" }]);
  assertEqual(password("abcdefgh"), [{ field: "password", message: "must contain a letter and a digit" }]);
  assertEqual(password("12345678"), [{ field: "password", message: "must contain a letter and a digit" }]);
  assertEqual(password("1234567é"), [{ field: "password", message: "must contain a letter and a digit" }]);
  assertEqual(password("xGRACE_h9"), [{ field: "password", message: "must not contain the username" }]);
  assertEqual(password("  pass word 1 "), []);
}

export function testPasswordNotTrimmed(): void {
  // 7 characters plus spaces is long enough, spaces count
  assertEqual(password(" abc12 "), [{ field: "password", message: "must be at least 8 characters" }]);
  assertEqual(password(" abc123 "), []);
}

export function testUsernameCheckUsesTrimmedName(): void {
  const f = valid();
  f.username = " bob ";
  f.password = "xxbobxx12";
  f.confirmPassword = "xxbobxx12";
  assertEqual(validateSignup(f), [{ field: "password", message: "must not contain the username" }]);
  // no username: nothing to compare with
  f.username = "";
  assertEqual(validateSignup(f), [{ field: "username", message: "is required" }]);
}

export function testConfirmPassword(): void {
  const f = valid();
  f.confirmPassword = "cobol1960";
  assertEqual(validateSignup(f), [{ field: "confirmPassword", message: "does not match" }]);
  // checked even when the password has its own error
  f.password = "short1";
  f.confirmPassword = "short2";
  assertEqual(validateSignup(f), [
    { field: "password", message: "must be at least 8 characters" },
    { field: "confirmPassword", message: "does not match" },
  ]);
  // skipped when the password is empty
  f.password = "";
  assertEqual(validateSignup(f), [{ field: "password", message: "is required" }]);
}

export function testAgeRules(): void {
  assertEqual(age(""), []);
  assertEqual(age("   "), []);
  assertEqual(age("13"), []);
  assertEqual(age("12"), ["must be between 13 and 120"]);
  assertEqual(age("121"), ["must be between 13 and 120"]);
  assertEqual(age("013"), []);
  assertEqual(age("abc"), ["must be a whole number"]);
  assertEqual(age("18.5"), ["must be a whole number"]);
  assertEqual(age("-20"), ["must be a whole number"]);
  assertEqual(age("1e2"), ["must be a whole number"]);
  assertEqual(age("2 0"), ["must be a whole number"]);
}

export function testCountry(): void {
  const f = valid();
  f.country = "";
  assertEqual(validateSignup(f), [{ field: "country", message: "is required" }]);
  f.country = "  ";
  assertEqual(validateSignup(f), [{ field: "country", message: "is required" }]);
  f.country = "Ca";
  assertEqual(validateSignup(f), []);
  f.country = "MX";
  assertEqual(validateSignup(f), [{ field: "country", message: "is not supported" }]);
  f.country = "USA";
  assertEqual(validateSignup(f), [{ field: "country", message: "is not supported" }]);
}

export function testEverythingWrongInFieldOrder(): void {
  const f: SignupForm = {
    username: "9lives", email: "cat@home", password: "lives", confirmPassword: "", age: "old",
    country: "XX", acceptTerms: false,
  };
  assertEqual(validateSignup(f).map((e) => `${e.field}: ${e.message}`), [
    "username: must start with a letter",
    "email: is not a valid email address",
    "password: must be at least 8 characters",
    "confirmPassword: does not match",
    "age: must be a whole number",
    "country: is not supported",
    "acceptTerms: must be accepted",
  ]);
}
