import { assertEqual } from "e7:test";
import { quoteShipping } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

export function testVolumetricWeight(): void {
  const request = '{"country":"fr","postcode":"75001","parcels":[{"weightKg":2,"lengthCm":40,"widthCm":30,"heightCm":20}]}';
  assertEqual(quoteShipping(request), '{"zone":"eu","remote":false,"quotes":[{"service":"standard","cents":1799,"price":"EUR 17.99","surcharges":[]},{"service":"express","cents":3787,"price":"EUR 37.87","surcharges":["fuel"]}]}');
}

export function testHalfKiloRoundingAtTableEdge(): void {
  const request = '{"country":"AT","postcode":"1010","parcels":[{"weightKg":1.95,"lengthCm":20,"widthCm":20,"heightCm":10},{"weightKg":2,"lengthCm":10,"widthCm":10,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"zone":"eu","remote":false,"quotes":[{"service":"standard","cents":2598,"price":"EUR 25.98","surcharges":[]},{"service":"express","cents":7574,"price":"EUR 75.74","surcharges":["fuel"]}]}');
}

export function testRemoteGermanIsland(): void {
  const request = '{"country":"DE","postcode":"18565","parcels":[{"weightKg":3,"lengthCm":30,"widthCm":30,"heightCm":30}]}';
  assertEqual(quoteShipping(request), '{"zone":"domestic","remote":true,"quotes":[{"service":"standard","cents":2299,"price":"EUR 22.99","surcharges":["remote area"]},{"service":"economy","cents":2399,"price":"EUR 23.99","surcharges":["remote area"]},{"service":"express","cents":4202,"price":"EUR 42.02","surcharges":["remote area","fuel"]}]}');
}

export function testRemoteUkPostcodeCaseAndSpaces(): void {
  const request = '{"country":" gb ","postcode":"hs1 2ab","parcels":[{"weightKg":4,"lengthCm":30,"widthCm":20,"heightCm":20}]}';
  assertEqual(quoteShipping(request), '{"zone":"europe","remote":true,"quotes":[{"service":"standard","cents":3799,"price":"EUR 37.99","surcharges":["remote area"]},{"service":"express","cents":6372,"price":"EUR 63.72","surcharges":["remote area","fuel"]}]}');
}

export function testUkIvPrefixNeedsDigit(): void {
  const request = '{"country":"GB","postcode":"IV63 6TU","parcels":[{"weightKg":4,"lengthCm":30,"widthCm":20,"heightCm":20}]}';
  assertEqual(quoteShipping(request), '{"zone":"europe","remote":false,"quotes":[{"service":"standard","cents":2299,"price":"EUR 22.99","surcharges":[]},{"service":"express","cents":4872,"price":"EUR 48.72","surcharges":["fuel"]}]}');
}

export function testDanishRemoteNeedsFourDigits(): void {
  const request = '{"country":"DK","postcode":"3700","parcels":[{"weightKg":1,"lengthCm":10,"widthCm":10,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"zone":"eu","remote":true,"quotes":[{"service":"standard","cents":2799,"price":"EUR 27.99","surcharges":["remote area"]},{"service":"express","cents":5287,"price":"EUR 52.87","surcharges":["remote area","fuel"]}]}');
}

export function testDanishFiveDigitsNotRemote(): void {
  const request = '{"country":"DK","postcode":"37000","parcels":[{"weightKg":1,"lengthCm":10,"widthCm":10,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"zone":"eu","remote":false,"quotes":[{"service":"standard","cents":1299,"price":"EUR 12.99","surcharges":[]},{"service":"express","cents":3787,"price":"EUR 37.87","surcharges":["fuel"]}]}');
}

export function testOversizeByGirth(): void {
  const request = '{"country":"DE","postcode":"80331","parcels":[{"weightKg":3,"lengthCm":4,"widthCm":119,"heightCm":91}]}';
  assertEqual(quoteShipping(request), '{"zone":"domestic","remote":false,"quotes":[{"service":"standard","cents":3299,"price":"EUR 32.99","surcharges":["oversize"]},{"service":"economy","cents":3399,"price":"EUR 33.99","surcharges":["oversize"]},{"service":"express","cents":5202,"price":"EUR 52.02","surcharges":["oversize","fuel"]}]}');
}

export function testOversizeByLength(): void {
  const request = '{"country":"NL","postcode":"1012","parcels":[{"weightKg":3,"lengthCm":130,"widthCm":10,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"zone":"eu","remote":false,"quotes":[{"service":"standard","cents":4299,"price":"EUR 42.99","surcharges":["oversize"]},{"service":"express","cents":6287,"price":"EUR 62.87","surcharges":["oversize","fuel"]}]}');
}

export function testHeavyParcelDropsExpress(): void {
  const request = '{"country":"DE","postcode":"80331","parcels":[{"weightKg":24.2,"lengthCm":60,"widthCm":40,"heightCm":40}]}';
  assertEqual(quoteShipping(request), '{"zone":"domestic","remote":false,"quotes":[{"service":"economy","cents":1702,"price":"EUR 17.02","surcharges":["heavy"]},{"service":"standard","cents":1902,"price":"EUR 19.02","surcharges":["heavy"]}]}');
}

export function testTooHeavyForWorld(): void {
  const request = '{"country":"US","postcode":"10001","parcels":[{"weightKg":21,"lengthCm":50,"widthCm":50,"heightCm":50},{"weightKg":1,"lengthCm":10,"widthCm":10,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"zone":"world","remote":false,"quotes":[]}');
}

export function testMissingPostcodeAndNoParcels(): void {
  assertEqual(quoteShipping('{"country":"JP","parcels":[]}'), '{"zone":"world","remote":false,"quotes":[]}');
}

export function testInvalidParcel(): void {
  const request = '{"country":"CH","postcode":"8001","parcels":[{"weightKg":1,"lengthCm":10,"widthCm":10,"heightCm":10},{"weightKg":2,"lengthCm":10,"widthCm":0,"heightCm":10}]}';
  assertEqual(quoteShipping(request), '{"error":"parcel 2: invalid size or weight"}');
}

export function testInvalidJson(): void {
  assertEqual(quoteShipping('{"country":"DE","parcels":[{'), '{"error":"invalid json"}');
}
