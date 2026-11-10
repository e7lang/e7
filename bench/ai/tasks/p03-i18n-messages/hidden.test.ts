import { assertEqual } from "e7:test";
import { translate } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

const CATALOG = '{' +
  '"en":{"inbox.title":"Inbox","inbox.count.zero":"No new messages","inbox.count.one":"One new message","inbox.count.other":"{count} new messages","greeting":"Hello, {name}!","order.total":"Total: {amount} {currency}","files.count.other":"{count} files in {folder}","help.link":"See {url} or {missing}"},' +
  '"de":{"inbox.title":"Posteingang","inbox.count.one":"Eine neue Nachricht","inbox.count.other":"{count} neue Nachrichten","order.total":"Summe: {amount} {currency}"},' +
  '"pt-BR":{"inbox.title":"Caixa de entrada","greeting":"Olá, {name}!"},' +
  '"pt":{"inbox.title":"Caixa","order.total":"Total: {amount}"},' +
  '"fr":{"inbox.count.one":"{count} nouveau message","inbox.count.other":"{count} nouveaux messages","order.total":"Total : {amount} {currency}"},' +
  '"pl":{"inbox.count.one":"Jedna nowa wiadomość","inbox.count.few":"{count} nowe wiadomości","inbox.count.other":"{count} nowych wiadomości"},' +
  '"ja":{"inbox.count.other":"新着メッセージ{count}件","greeting":"{ name }さん、こんにちは"}' +
  '}';

export function testRegionFallsBackToLanguage(): void {
  assertEqual(translate(CATALOG, 'pt_br', 'order.total', '{"amount":"1500.5","currency":"BRL"}'), 'Total: 1,500.50');
}

export function testRegionMessageWins(): void {
  assertEqual(translate(CATALOG, 'PT-br', 'inbox.title', ''), 'Caixa de entrada');
}

export function testFallbackToEnglishKeepsMissingParam(): void {
  assertEqual(translate(CATALOG, 'pt-PT', 'help.link', '{"url":"https://x.io/help","count":"3"}'), 'See https://x.io/help or {missing}');
}

export function testUnknownLocaleTagUsesDefault(): void {
  assertEqual(translate(CATALOG, 'x', 'inbox.title', '{}'), 'Inbox');
}

export function testZeroForm(): void {
  assertEqual(translate(CATALOG, 'en', 'inbox.count', '{"count":"0"}'), 'No new messages');
}

export function testZeroWithoutZeroFormInGerman(): void {
  assertEqual(translate(CATALOG, 'de', 'inbox.count', '{"count":"0"}'), '0 neue Nachrichten');
}

export function testFrenchZeroIsOne(): void {
  assertEqual(translate(CATALOG, 'fr', 'inbox.count', '{"count":"0"}'), '0 nouveau message');
}

export function testFrenchGrouping(): void {
  assertEqual(translate(CATALOG, 'fr', 'inbox.count', '{"count":"1234567"}'), '1 234 567 nouveaux messages');
}

export function testPolishFew(): void {
  assertEqual(translate(CATALOG, 'pl', 'inbox.count', '{"count":"22"}'), '22 nowe wiadomości');
}

export function testPolishTeens(): void {
  assertEqual(translate(CATALOG, 'pl', 'inbox.count', '{"count":"13"}'), '13 nowych wiadomości');
}

export function testJapaneseNegativeCount(): void {
  assertEqual(translate(CATALOG, 'ja', 'inbox.count', '{"count":"-12345"}'), '新着メッセージ-12,345件');
}

export function testDecimalsAndNonNumericParam(): void {
  assertEqual(translate(CATALOG, 'de', 'order.total', '{"amount":"-1234.567","currency":"1.000 EUR"}'), 'Summe: -1.234,57 1.000 EUR');
}

export function testSpacedPlaceholderIsNotReplaced(): void {
  assertEqual(translate(CATALOG, 'ja', 'greeting', '{"name":"Aiko"}'), '{ name }さん、こんにちは');
}

export function testMissingKeyShowsKey(): void {
  assertEqual(translate(CATALOG, 'en', 'files.count', '{"folder":"Docs"}'), 'files.count');
}

export function testInvalidParams(): void {
  assertEqual(translate(CATALOG, 'en', 'greeting', '{"name": '), '[greeting: SyntaxError]');
}
