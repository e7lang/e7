// Ed25519: small-order / non-canonical keys and R are rejected (stricter than OpenSSL, which accepts some)
import { ed25519Verify } from "e7:crypto";
import { hexDecode, utf8Encode } from "e7:encoding";
const pk = hexDecode("fd1724385aa0c75b64fb78cd602fa1d991fdebf76b13c58ed702eac835e9f618");
const m = utf8Encode("pay 10");
console.log("orig", ed25519Verify(pk, m, hexDecode("c6512c58648510323eb612b4fda412a156146eb653269d6624aa7576ae6d15de6f7e88edcb99ca60eb4e1b21ca0c153feea4b2d49ee43f9cbe8a34dd28aada05")));
console.log("R+T4", ed25519Verify(pk, m, hexDecode("bf00624b159e08aa6523c515bd17fd910a4bc7a85430feae9288308cd18d4c646f7e88edcb99ca60eb4e1b21ca0c153feea4b2d49ee43f9cbe8a34dd28aada05")));
const z = new Uint8Array(32), zs = new Uint8Array(64);
let n = 0;
for (let i = 0; i < 100; i++) if (ed25519Verify(z, utf8Encode("any message " + i), zs)) n++;
console.log("lowOrderPk zero-sig accepted", n, "/100");
