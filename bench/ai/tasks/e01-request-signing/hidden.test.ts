import { assertEqual } from "e7:test";
import { hmac } from "e7:crypto";
import { hexDecode, hexEncode, utf8Encode } from "e7:encoding";
import { canonicalQuery, canonicalRequest, signRequest, verifyRequest } from "./solution.ts";
import type { ApiRequest } from "./solution.ts";

const secret = "000102030405060708090a0b0c0d0e0f";
const oldSecret = "ffeeddccbbaa99887766554433221100";
const emptySha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
const emptySha512 =
  "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e";
const abcSha512 =
  "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f";
const now = 1700000000;

function mac(alg: string, key: string, text: string): string {
  return hexEncode(hmac(alg, hexDecode(key), utf8Encode(text)));
}

function getOrders(): ApiRequest {
  return {
    method: "get",
    path: "/v1/orders",
    query: "status=open&limit=10",
    headers: [{ name: "Host", value: " api.example.com " }, { name: "Accept", value: "*/*" }],
    body: "",
  };
}

function postPayment(): ApiRequest {
  return {
    method: "POST",
    path: "/v1/pay",
    query: "",
    headers: [
      { name: "X-Request-Id", value: "r-1" },
      { name: "Content-Type", value: "application/json" },
      { name: "host", value: "api.example.com" },
      { name: "x-request-id", value: "r-2" },
    ],
    body: "abc",
  };
}

function keys(): Map<string, string> {
  return new Map<string, string>([["k1", secret], ["k0", oldSecret]]);
}

export function testSha256StillDefault(): void {
  const canonical = `GET\n/v1/orders\nlimit=10&status=open\nhost:api.example.com\nx-ts:${now}\n${emptySha256}`;
  assertEqual(canonicalRequest(getOrders(), now), canonical);
  assertEqual(signRequest(getOrders(), "k1", secret, now), `E7-HMAC-SHA256 keyId=k1,ts=${now},headers=host,sig=${mac("sha256", secret, canonical)}`);
  assertEqual(signRequest(getOrders(), "k1", secret, now, "sha256"), signRequest(getOrders(), "k1", secret, now));
}

export function testCanonicalSha512UsesSha512BodyHash(): void {
  assertEqual(
    canonicalRequest(getOrders(), now, "sha512"),
    `GET\n/v1/orders\nlimit=10&status=open\nhost:api.example.com\nx-ts:${now}\n${emptySha512}`,
  );
}

export function testSignSha512(): void {
  const canonical =
    `POST\n/v1/pay\n\ncontent-type:application/json\nhost:api.example.com\nx-request-id:r-1,r-2\nx-ts:${now}\n${abcSha512}`;
  assertEqual(canonicalRequest(postPayment(), now, "sha512"), canonical);
  assertEqual(
    signRequest(postPayment(), "k1", secret, now, "sha512"),
    `E7-HMAC-SHA512 keyId=k1,ts=${now},headers=content-type;host;x-request-id,sig=${mac("sha512", secret, canonical)}`,
  );
}

export function testCanonicalQueryRegression(): void {
  assertEqual(canonicalQuery("b=2&a=2&a=1&&z"), "a=2&a=1&b=2&z=");
  assertEqual(canonicalQuery("x=1=2"), "x=1=2");
}

export function testVerifyValid(): void {
  const r = postPayment();
  assertEqual(verifyRequest(signRequest(r, "k1", secret, now), r, keys(), now), "");
  assertEqual(verifyRequest(signRequest(r, "k1", secret, now, "sha512"), r, keys(), now + 10), "");
  assertEqual(verifyRequest(signRequest(r, "k0", oldSecret, now, "sha512"), r, keys(), now), "");
}

export function testVerifyParamsInAnyOrder(): void {
  const r = getOrders();
  const canonical = canonicalRequest(r, now);
  const sig = mac("sha256", secret, canonical);
  assertEqual(verifyRequest(`E7-HMAC-SHA256 sig=${sig},ts=${now},keyId=k1`, r, keys(), now), "");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=${now},sig=${sig},v=2`, r, keys(), now), "");
}

export function testVerifyUnknownKey(): void {
  const r = getOrders();
  assertEqual(verifyRequest(signRequest(r, "k9", secret, now), r, keys(), now), "unknown key");
}

export function testVerifyStale(): void {
  const r = getOrders();
  const h = signRequest(r, "k1", secret, now);
  assertEqual(verifyRequest(h, r, keys(), now + 300), "");
  assertEqual(verifyRequest(h, r, keys(), now - 300), "");
  assertEqual(verifyRequest(h, r, keys(), now + 301), "stale");
  assertEqual(verifyRequest(h, r, keys(), now - 301), "stale");
}

export function testVerifyBadSignature(): void {
  const r = postPayment();
  const h = signRequest(r, "k1", secret, now);
  const tampered = postPayment();
  tampered.body = "abd";
  assertEqual(verifyRequest(h, tampered, keys(), now), "bad signature");
  const otherQuery = postPayment();
  otherQuery.query = "dry=1";
  assertEqual(verifyRequest(h, otherQuery, keys(), now), "bad signature");
  // signed with k0's secret but claims k1
  assertEqual(verifyRequest(signRequest(r, "k1", oldSecret, now), r, keys(), now), "bad signature");
}

export function testVerifyAlgorithmIsBound(): void {
  const r = getOrders();
  const h512 = signRequest(r, "k1", secret, now, "sha512");
  assertEqual(verifyRequest(h512.replace("E7-HMAC-SHA512", "E7-HMAC-SHA256"), r, keys(), now), "bad signature");
}

export function testVerifyUnknownAlgorithm(): void {
  const r = getOrders();
  const h = signRequest(r, "k1", secret, now);
  assertEqual(verifyRequest(h.replace("E7-HMAC-SHA256", "E7-HMAC-SHA1"), r, keys(), now), "unknown algorithm");
  assertEqual(verifyRequest("Bearer abc", r, keys(), now), "unknown algorithm");
}

export function testVerifyMalformed(): void {
  const r = getOrders();
  assertEqual(verifyRequest("E7-HMAC-SHA256", r, keys(), now), "malformed");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=${now}`, r, keys(), now), "malformed");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=12a,sig=00`, r, keys(), now), "malformed");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=,sig=00`, r, keys(), now), "malformed");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=${now},junk,sig=00`, r, keys(), now), "malformed");
}

export function testVerifyCheckOrder(): void {
  const r = getOrders();
  // unknown key is reported before stale, stale before a bad signature
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k9,ts=1,sig=00`, r, keys(), now), "unknown key");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=1,sig=00`, r, keys(), now), "stale");
  assertEqual(verifyRequest(`E7-HMAC-SHA256 keyId=k1,ts=${now},sig=00`, r, keys(), now), "bad signature");
}
