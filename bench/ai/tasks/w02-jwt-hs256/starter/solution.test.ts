import { assertEqual } from "e7:test";
import { signJwt, verifyJwt } from "./solution.ts";

export function testSignKnownVector(): void {
  // the example token from jwt.io
  const claims = '{"sub":"1234567890","name":"John Doe","iat":1516239022}';
  assertEqual(
    signJwt(claims, "your-256-bit-secret"),
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c",
  );
}

export function testRoundTrip(): void {
  const claims = '{"sub":"u1","exp":1700000600}';
  const token = signJwt(claims, "s3cr3t");
  assertEqual(verifyJwt(token, "s3cr3t", 1700000000), { valid: true, error: "", claimsJson: claims });
  assertEqual(verifyJwt(token, "s3cr3t", 1700000600), { valid: false, error: "expired", claimsJson: "" });
}
