import { assertEqual, assertThrows } from "e7:test";
import { hmac } from "e7:crypto";
import { hexDecode, hexEncode, utf8Encode } from "e7:encoding";
import { canonicalQuery, canonicalRequest, signRequest } from "./solution.ts";
import type { ApiRequest } from "./solution.ts";

const secret = "000102030405060708090a0b0c0d0e0f";
const emptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

function getOrders(): ApiRequest {
  return {
    method: "get",
    path: "/v1/orders",
    query: "status=open&limit=10",
    headers: [{ name: "Host", value: " api.example.com " }, { name: "Accept", value: "*/*" }],
    body: "",
  };
}

export function testCanonicalQuery(): void {
  assertEqual(canonicalQuery("b=2&a=1&&c"), "a=1&b=2&c=");
  assertEqual(canonicalQuery(""), "");
}

export function testCanonicalRequest(): void {
  assertEqual(
    canonicalRequest(getOrders(), 1700000000),
    `GET\n/v1/orders\nlimit=10&status=open\nhost:api.example.com\nx-ts:1700000000\n${emptySha256}`,
  );
}

export function testSignRequest(): void {
  const canonical = canonicalRequest(getOrders(), 1700000000);
  const sig = hexEncode(hmac("sha256", hexDecode(secret), utf8Encode(canonical)));
  assertEqual(signRequest(getOrders(), "k1", secret, 1700000000), `E7-HMAC-SHA256 keyId=k1,ts=1700000000,headers=host,sig=${sig}`);
}

export function testBadInput(): void {
  assertThrows(() => { signRequest(getOrders(), "a,b", secret, 1); }, "bad key id");
  assertThrows(() => { signRequest(getOrders(), "k1", secret, 1.5); }, "bad timestamp");
}
