import { assertEqual } from "e7:test";
import { parseQuery, buildQuery, getParam, setParam } from "./solution.ts";

export function testParse(): void {
  assertEqual(parseQuery("?q=caf%C3%A9+au+lait&tag=a&tag=b"), [
    { key: "q", value: "café au lait" },
    { key: "tag", value: "a" },
    { key: "tag", value: "b" },
  ]);
}

export function testBuild(): void {
  assertEqual(buildQuery([{ key: "q", value: "50% off!" }, { key: "page", value: "2" }]), "q=50%25+off%21&page=2");
}

export function testSetAndGet(): void {
  assertEqual(setParam("https://shop.example/s?page=1&sort=new#top", "page", "2"), "https://shop.example/s?page=2&sort=new#top");
  assertEqual(getParam("a=1&b=2", "b"), "2");
}
