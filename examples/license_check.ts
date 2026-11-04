// Offline license verification with Ed25519: the vendor signs "<user>|<expiry>|<features>",
// the app embeds only the public key.
import { ed25519Sign, ed25519Verify, ed25519PublicKey, sha256 } from "e7:crypto";
import { base64Encode, base64Decode, utf8Encode } from "e7:encoding";

interface License { user: string; expires: number; features: string[] }

export function issue(lic: License, vendorSeed: Uint8Array): string {
  const payload = `${lic.user}|${lic.expires}|${lic.features.join(",")}`;
  return payload + "." + base64Encode(ed25519Sign(vendorSeed, utf8Encode(payload)));
}

export function check(token: string, vendorPublicKey: Uint8Array, today: number): License | null {
  const dot = token.lastIndexOf(".");
  if (dot < 0) return null;
  const payload = token.slice(0, dot);
  if (!ed25519Verify(vendorPublicKey, utf8Encode(payload), base64Decode(token.slice(dot + 1)))) return null;
  const [user, exp, feats] = payload.split("|");
  const expires = Number(exp);
  if (!(expires >= today)) return null;
  return { user, expires, features: feats === "" ? [] : feats.split(",") };
}

const seed = sha256(utf8Encode("vendor signing seed"));
const pub = ed25519PublicKey(seed);
const tok = issue({ user: "alice@example.com", expires: 20991231, features: ["pro", "export"] }, seed);
console.log(tok);
const lic = check(tok, pub, 20250101);
console.log(lic === null ? "invalid" : `${lic.user} ${lic.features.join("+")}`);
console.log(check(tok.replace("alice", "mallory"), pub, 20250101) === null ? "tampered: rejected" : "accepted?!");
