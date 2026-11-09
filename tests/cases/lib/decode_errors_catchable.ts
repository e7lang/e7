// Invalid encoded input is a catchable SyntaxError (input to a decoder usually comes from outside).
import { hexDecode, base64Decode, base64UrlDecode, base32Decode, pemDecode, hexEncode } from "e7:encoding";

function attempt(name: string, f: () => Uint8Array): void {
  try {
    console.log(name, "ok", hexEncode(f()));
  } catch (e) {
    console.log(name, e instanceof SyntaxError, (e as Error).message);
  }
}
attempt("hex odd", () => hexDecode("abc"));
attempt("hex char", () => hexDecode("zz"));
attempt("hex ok", () => hexDecode("0aFF"));
attempt("b64 char", () => base64Decode("YW*I"));
attempt("b64 len", () => base64Decode("YWJjZ"));
attempt("b64 pad", () => base64Decode("YQ==="));
attempt("b64 ok", () => base64Decode("YWI="));
attempt("b64 ws", () => base64Decode(" YW\nJj "));
attempt("b64url ok", () => base64UrlDecode("-_8"));
attempt("b64url char", () => base64UrlDecode("a.b"));
attempt("b64 non-ascii", () => base64Decode("YWé="));
attempt("b32 char", () => base32Decode("MZXW1"));
attempt("b32 pad", () => base32Decode("MY=A"));
attempt("b32 ok", () => base32Decode("mzxw6==="));
attempt("pem none", () => pemDecode("hello"));
attempt("pem body", () => pemDecode("-----BEGIN X-----\n#$%\n-----END X-----\n"));
attempt("pem ok", () => pemDecode("-----BEGIN X-----\nAAEC\n-----END X-----\n"));
