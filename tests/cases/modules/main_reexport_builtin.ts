// functions of a builtin module re-exported by a module, imported by name and through namespaces
import { hex, utf8Encode, enc, base64Encode } from "./_lib/reexport_builtin.ts";
import * as B from "./_lib/reexport_builtin.ts";
const bytes = utf8Encode("hi");
console.log(hex(bytes), enc.base64Encode(bytes), base64Encode(B.utf8Encode("x")), B.enc.hexEncode(B.utf8Encode("a")));
console.log(B.hex(B.hexDecode("ff00")), B.utf8Decode(enc.base64Decode("aGk=")));
