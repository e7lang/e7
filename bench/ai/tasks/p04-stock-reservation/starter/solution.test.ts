import { assertEqual } from "e7:test";
import { reserveStock } from "./solution.ts";

const STOCK = '{' +
  '"MUG-01":{"onHand":20,"reserved":5,"safety":3},' +
  '"TEE-M":{"onHand":4,"reserved":1,"backorderable":true,"leadDays":7},' +
  '"TEE-L":{"onHand":2,"reserved":2,"backorderable":true},' +
  '"CAP":{"onHand":10,"reserved":0},' +
  '"PIN":{"onHand":3,"reserved":4,"safety":1}' +
  '}';

export function testReserveAll(): void {
  const request = '{"orderId":"A-100","mode":"all-or-nothing","lines":[{"sku":"MUG-01","qty":2},{"sku":"CAP","qty":10}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"A-100","committed":true,"lines":[{"status":"reserved","sku":"MUG-01","qty":2},{"status":"reserved","sku":"CAP","qty":10}],"remaining":[["MUG-01",10],["CAP",0]]}');
}

export function testPartialWithBackorder(): void {
  const request = '{"orderId":"A-101","mode":"partial","lines":[{"sku":"TEE-M","qty":5},{"sku":"CAP","qty":1}]}';
  assertEqual(reserveStock(STOCK, request), '{"orderId":"A-101","committed":true,"lines":[{"status":"split","sku":"TEE-M","reserved":3,"backordered":2,"etaDays":7},{"status":"reserved","sku":"CAP","qty":1}],"remaining":[["TEE-M",0],["CAP",9]]}');
}
