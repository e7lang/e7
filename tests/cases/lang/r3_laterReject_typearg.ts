// third test round of 0.1.3 (laterReject_typearg): must print what Node.js prints
import { laterReject } from "e7:test";
async function f(): Promise<string> {
  return laterReject<string>(new Error("x"));
}
f().then((s) => console.log(s), (e: Error) => console.log("caught", e.message));
