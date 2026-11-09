We're moving signup validation into a shared E7 script so the iOS app, the Android app and the backend all
report exactly the same errors. Please write `solution.ts` with:

```ts
export interface SignupForm {
  username: string;
  email: string;
  password: string;
  confirmPassword: string;
  age: string;        // raw text from the input box, may be empty
  country: string;
  acceptTerms: boolean;
}
export interface FieldError { field: string; message: string }
export function validateSignup(form: SignupForm): FieldError[]
```

Return every error, at most one per field (the first rule that fails, in the order listed), in this field
order: username, email, password, confirmPassword, age, country, acceptTerms. A valid form gives `[]`.
`field` is the property name exactly as in `SignupForm`.

- **username** (leading/trailing spaces ignored):
  empty → `"is required"`; fewer than 3 or more than 20 characters → `"must be 3 to 20 characters"`;
  any character other than ASCII letters, digits and `_` → `"may only contain letters, digits and _"`;
  does not start with a letter → `"must start with a letter"`.
- **email** (leading/trailing spaces ignored): empty → `"is required"`; otherwise it is
  `"is not a valid email address"` unless it has exactly one `@`, no spaces, a non-empty part before the `@`,
  and a domain after it that contains a `.` which is neither its first nor its last character.
- **password** (taken as is, not trimmed): empty → `"is required"`; shorter than 8 characters →
  `"must be at least 8 characters"`; no ASCII letter or no digit → `"must contain a letter and a digit"`;
  contains the (trimmed, non-empty) username, ignoring case → `"must not contain the username"`.
- **confirmPassword**: `"does not match"` when it differs from the password. Skip this check when the
  password is empty (that is already reported); do the check even if the password has another error.
- **age** (spaces around ignored): optional, empty is fine; anything but plain digits →
  `"must be a whole number"`; outside 13..120 → `"must be between 13 and 120"`.
- **country** (spaces ignored, any case): empty → `"is required"`; not one of US, CA, GB, DE, FR, JP →
  `"is not supported"`.
- **acceptTerms**: false → `"must be accepted"`.

`solution.test.ts` has a couple of examples.
