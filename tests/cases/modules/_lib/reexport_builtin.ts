// re-exports of a builtin module: named, as a namespace, and every function (export *)
export { hexEncode as hex, utf8Encode } from "e7:encoding";
export * as enc from "e7:encoding";
export * from "e7:encoding";
