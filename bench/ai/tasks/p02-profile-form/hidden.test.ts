import { assertEqual } from "e7:test";
import { submitProfileForm } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

export function testAllFieldsNormalized(): void {
  const form = '{"displayName":"Zoë\\tO\'Neil","email":" zoe@mail.example.org ","phone":"+1 (212) 555 0199","age":" 42 ","website":"HTTP://Example.com/zoe/","tags":"Art; music , art,, MUSIC ;film"}';
  assertEqual(submitProfileForm(form), '{"ok":true,"profile":{"displayName":"Zoë O\'Neil","email":"zoe@mail.example.org","phone":"(212) 555-0199","age":42,"website":"https://example.com/zoe","tags":["art","music","film"]},"ignored":[]}');
}

export function testAliasesAndIgnoredFields(): void {
  const form = '{"name":"Carla","mail":"carla@x.io","nickname":"cc","tel":"2125550100","url":"carla.dev","theme":"dark"}';
  assertEqual(submitProfileForm(form), '{"ok":true,"profile":{"displayName":"Carla","email":"carla@x.io","phone":"(212) 555-0100","website":"https://carla.dev","tags":[]},"ignored":["nickname","theme"]}');
}

export function testLaterKeyWins(): void {
  assertEqual(submitProfileForm('{"displayName":"First Name","name":"Second Name","email":"a@b.co"}'), '{"ok":true,"profile":{"displayName":"Second Name","email":"a@b.co","tags":[]},"ignored":[]}');
}

export function testBlankOptionalFieldsDropped(): void {
  const form = '{"displayName":"Dee","email":"dee@d.de","phone":"   ","age":"","website":" ","tags":" , ; "}';
  assertEqual(submitProfileForm(form), '{"ok":true,"profile":{"displayName":"Dee","email":"dee@d.de","tags":[]},"ignored":[]}');
}

export function testBlankRequiredField(): void {
  assertEqual(submitProfileForm('{"displayName":"   ","email":"e@e.ee"}'), '{"ok":false,"errors":["displayName: is required"]}');
}

export function testEveryErrorInFieldOrder(): void {
  const form = '{"tags":"a,b,c,d,e,f","website":"not a site","age":"12","phone":"12345","email":"no-at-sign.com","displayName":"X"}';
  assertEqual(submitProfileForm(form), '{"ok":false,"errors":["displayName: must be 2-40 characters","email: not a valid email address","phone: must have 10 digits","age: must be between 13 and 120","website: not a valid URL","tags: at most 5 tags"]}');
}

export function testEmailDomainWithoutDot(): void {
  assertEqual(submitProfileForm('{"displayName":"Fay","email":"fay@localhost"}'), '{"ok":false,"errors":["email: not a valid email address"]}');
}

export function testEmailWithSpace(): void {
  assertEqual(submitProfileForm('{"displayName":"Gil","email":"gil @x.com"}'), '{"ok":false,"errors":["email: not a valid email address"]}');
}

export function testAgeNotWhole(): void {
  assertEqual(submitProfileForm('{"displayName":"Hal","email":"hal@h.hh","age":"30.5"}'), '{"ok":false,"errors":["age: must be a whole number"]}');
}

export function testAgeBounds(): void {
  assertEqual(submitProfileForm('{"displayName":"Ivy","email":"ivy@i.ii","age":"120","phone":"1-800-555-0000"}'), '{"ok":true,"profile":{"displayName":"Ivy","email":"ivy@i.ii","phone":"(800) 555-0000","age":120,"tags":[]},"ignored":[]}');
}

export function testElevenDigitsNotStartingWithOne(): void {
  assertEqual(submitProfileForm('{"displayName":"Kim","email":"kim@k.kk","phone":"25551234567"}'), '{"ok":false,"errors":["phone: must have 10 digits"]}');
}

export function testWebsiteWithoutDot(): void {
  assertEqual(submitProfileForm('{"displayName":"Lu","email":"lu@l.ll","website":"https://localhost/"}'), '{"ok":false,"errors":["website: not a valid URL"]}');
}

export function testWebsiteHttpsPathKeptCase(): void {
  const form = '{"displayName":"Mo","email":"mo@m.mm","website":"https://Shop.Example.COM/Path/To//"}';
  assertEqual(submitProfileForm(form), '{"ok":true,"profile":{"displayName":"Mo","email":"mo@m.mm","website":"https://shop.example.com/Path/To","tags":[]},"ignored":[]}');
}

export function testNameTooLong(): void {
  assertEqual(submitProfileForm('{"displayName":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA","email":"n@n.nn"}'), '{"ok":false,"errors":["displayName: must be 2-40 characters"]}');
}

export function testInvalidJson(): void {
  assertEqual(submitProfileForm('{"displayName": "x",'), '{"ok":false,"errors":["form: invalid json"]}');
}
