import { assertEqual } from "e7:test";
import { canonicalQuery, stringToSign, signWithSecret } from "./request_signing.ts";

export function testQueryIsSortedAndEncoded(): void {
  const p = new Map<string, string>([["b", "x y"], ["a", "1&2"]]);
  assertEqual(canonicalQuery(p), "a=1%262&b=x%20y");
}

export function testStringToSign(): void {
  const s = stringToSign({ method: "get", path: "/v1/x", params: new Map<string, string>(), body: "", timestamp: 1 });
  assertEqual(s, "GET\n/v1/x\n\n1\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

export function testSignatureIsStable(): void {
  const json = '{"method":"get","path":"/","params":{},"body":"","timestamp":0}';
  assertEqual(signWithSecret(json, "k").length, 64);
  assertEqual(signWithSecret(json, "k"), signWithSecret(json, "k"));
}
