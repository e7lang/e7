// @runtime-error: scalar out of range
import { ecdsaPublicKey } from "e7:crypto";
console.log(ecdsaPublicKey("P-256", new Uint8Array(32)).length);
