import { assertEqual } from "e7:test";
import { translate } from "./solution.ts";

const CATALOG = '{' +
  '"en":{"inbox.title":"Inbox","inbox.count.zero":"No new messages","inbox.count.one":"One new message","inbox.count.other":"{count} new messages","greeting":"Hello, {name}!","order.total":"Total: {amount} {currency}","files.count.other":"{count} files in {folder}","help.link":"See {url} or {missing}"},' +
  '"de":{"inbox.title":"Posteingang","inbox.count.one":"Eine neue Nachricht","inbox.count.other":"{count} neue Nachrichten","order.total":"Summe: {amount} {currency}"},' +
  '"pt-BR":{"inbox.title":"Caixa de entrada","greeting":"Olá, {name}!"},' +
  '"pt":{"inbox.title":"Caixa","order.total":"Total: {amount}"},' +
  '"fr":{"inbox.count.one":"{count} nouveau message","inbox.count.other":"{count} nouveaux messages","order.total":"Total : {amount} {currency}"},' +
  '"pl":{"inbox.count.one":"Jedna nowa wiadomość","inbox.count.few":"{count} nowe wiadomości","inbox.count.other":"{count} nowych wiadomości"},' +
  '"ja":{"inbox.count.other":"新着メッセージ{count}件","greeting":"{ name }さん、こんにちは"}' +
  '}';

export function testGreeting(): void {
  assertEqual(translate(CATALOG, 'en', 'greeting', '{"name":"Ann"}'), 'Hello, Ann!');
}

export function testPluralWithFallback(): void {
  assertEqual(translate(CATALOG, 'de-AT', 'inbox.count', '{"count":"1234"}'), '1.234 neue Nachrichten');
}
