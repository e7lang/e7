import { assertEqual } from "e7:test";
import { validateSignup } from "./solution.ts";

export function testValidForm(): void {
  const form = {
    username: "ada_l", email: "ada@example.com", password: "engine1843", confirmPassword: "engine1843",
    age: "36", country: "GB", acceptTerms: true,
  };
  assertEqual(validateSignup(form), []);
}

export function testSeveralErrors(): void {
  const form = {
    username: "a", email: "nope", password: "secret", confirmPassword: "secret",
    age: "", country: "DE", acceptTerms: false,
  };
  assertEqual(validateSignup(form), [
    { field: "username", message: "must be 3 to 20 characters" },
    { field: "email", message: "is not a valid email address" },
    { field: "password", message: "must be at least 8 characters" },
    { field: "acceptTerms", message: "must be accepted" },
  ]);
}
