import { assertEqual } from "e7:test";
import { reserveStock } from "./solution.ts";

// Expected values were produced by running original.ts on Node.js.

const STOCK = '{' +
  '"MUG-01":{"onHand":20,"reserved":5,"safety":3},' +
  '"TEE-M":{"onHand":4,"reserved":1,"backorderable":true,"leadDays":7},' +
  '"TEE-L":{"onHand":2,"reserved":2,"backorderable":true},' +
  '"CAP":{"onHand":10,"reserved":0},' +
  '"PIN":{"onHand":3,"reserved":4,"safety":1}' +
  '}';

export function testSafetyBufferLimitsFreeStock(): void {
  const request = '{"orderId":"B-1","mode":"partial","lines":[{"sku":"MUG-01","qty":12},{"sku":"MUG-01","qty":1}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-1","committed":false,"lines":[{"status":"rejected","sku":"MUG-01","reason":"insufficient stock (12 available)"}],"remaining":[["MUG-01",12]]}');
}

export function testDuplicateLinesMerged(): void {
  const request = '{"orderId":"B-2","mode":"partial","lines":[{"sku":"CAP","qty":4},{"sku":"MUG-01","qty":1},{"sku":"CAP","qty":3}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-2","committed":true,"lines":[{"status":"reserved","sku":"CAP","qty":7},{"status":"reserved","sku":"MUG-01","qty":1}],"remaining":[["CAP",3],["MUG-01",11]]}');
}

export function testAllOrNothingRollsBack(): void {
  const request = '{"orderId":"B-3","mode":"all-or-nothing","lines":[{"sku":"CAP","qty":2},{"sku":"TEE-M","qty":4}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-3","committed":false,"lines":[{"status":"reserved","sku":"CAP","qty":2},{"status":"split","sku":"TEE-M","reserved":3,"backordered":1,"etaDays":7}],"remaining":[["CAP",10],["TEE-M",3]]}');
}

export function testBackorderedWhenNothingFree(): void {
  assertEqual(reserveStock(STOCK, '{"orderId":"B-4","mode":"partial","lines":[{"sku":"TEE-L","qty":3}]}'), '{"orderId":"B-4","committed":false,"lines":[{"status":"backordered","sku":"TEE-L","qty":3,"etaDays":14}],"remaining":[["TEE-L",0]]}');
}

export function testOverReservedStockIsZeroFree(): void {
  const request = '{"orderId":"B-5","mode":"partial","lines":[{"sku":"PIN","qty":1},{"sku":"CAP","qty":1}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-5","committed":true,"lines":[{"status":"rejected","sku":"PIN","reason":"insufficient stock (0 available)"},{"status":"reserved","sku":"CAP","qty":1}],"remaining":[["PIN",0],["CAP",9]]}');
}

export function testUnknownSkuAndZeroLine(): void {
  const request = '{"orderId":"B-6","mode":"partial","lines":[{"sku":"HAT","qty":1},{"sku":"CAP","qty":0},{"sku":"MUG-01","qty":0},{"sku":"MUG-01","qty":3}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-6","committed":true,"lines":[{"status":"rejected","sku":"HAT","reason":"unknown sku"},{"status":"reserved","sku":"MUG-01","qty":3}],"remaining":[["MUG-01",9]]}');
}

export function testInvalidQuantities(): void {
  const request = '{"orderId":"B-7","mode":"partial","lines":[{"sku":"CAP","qty":1.5},{"sku":"MUG-01","qty":-2},{"sku":"TEE-M","qty":1}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-7","committed":true,"lines":[{"status":"rejected","sku":"CAP","reason":"invalid quantity"},{"status":"rejected","sku":"MUG-01","reason":"invalid quantity"},{"status":"reserved","sku":"TEE-M","qty":1}],"remaining":[["TEE-M",2]]}');
}

export function testNothingReservedNotCommitted(): void {
  const request = '{"orderId":"B-8","mode":"partial","lines":[{"sku":"PIN","qty":2},{"sku":"TEE-L","qty":1}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"B-8","committed":false,"lines":[{"status":"rejected","sku":"PIN","reason":"insufficient stock (0 available)"},{"status":"backordered","sku":"TEE-L","qty":1,"etaDays":14}],"remaining":[["PIN",0],["TEE-L",0]]}');
}

export function testDefaultLeadTime(): void {
  assertEqual(reserveStock(STOCK, '{"orderId":"B-9","mode":"all-or-nothing","lines":[{"sku":"TEE-L","qty":1}]}'), '{"orderId":"B-9","committed":false,"lines":[{"status":"backordered","sku":"TEE-L","qty":1,"etaDays":14}],"remaining":[["TEE-L",0]]}');
}

export function testUnknownMode(): void {
  assertEqual(reserveStock(STOCK, '{"orderId":"B-10","mode":"fast","lines":[{"sku":"CAP","qty":1}]}'), '{"error":"unknown mode \\"fast\\""}');
}

export function testMissingOrderId(): void {
  assertEqual(reserveStock(STOCK, '{"orderId":"","mode":"partial","lines":[{"sku":"CAP","qty":1}]}'), '{"error":"missing orderId"}');
}

export function testEmptyOrder(): void {
  assertEqual(reserveStock(STOCK, '{"orderId":"B-11","mode":"all-or-nothing","lines":[]}'), '{"orderId":"B-11","committed":true,"lines":[],"remaining":[]}');
}

export function testInvalidJson(): void {
  assertEqual(reserveStock(STOCK, '{"orderId":"B-12","lines":['), '{"error":"invalid json"}');
}

export function testOwnStockTable(): void {
  const stock = '{"X":{"onHand":5,"reserved":0,"safety":0,"backorderable":false,"leadDays":3}}';
  const request = '{"orderId":"B-13","mode":"partial","lines":[{"sku":"X","qty":6}]}';
  assertEqual(reserveStock(stock, request), '{"orderId":"B-13","committed":false,"lines":[{"status":"rejected","sku":"X","reason":"insufficient stock (5 available)"}],"remaining":[["X",5]]}');
}
