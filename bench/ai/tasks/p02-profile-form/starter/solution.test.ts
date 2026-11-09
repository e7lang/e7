import { assertEqual } from "e7:test";
import { submitProfileForm } from "./solution.ts";

export function testCompleteForm(): void {
  const form = '{"displayName":"  Ann   Lee ","email":"Ann.Lee@Example.COM","phone":"555-123-4567","tags":"Hiking, travel"}';
  assertEqual(submitProfileForm(form), '{"ok":true,"profile":{"displayName":"Ann Lee","email":"ann.lee@example.com","phone":"(555) 123-4567","tags":["hiking","travel"]},"ignored":[]}');
}

export function testMissingEmail(): void {
  assertEqual(submitProfileForm('{"displayName":"Bob"}'), '{"ok":false,"errors":["email: is required"]}');
}
