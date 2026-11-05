// @feature: `const enum`
// @category: Enums
// @status: supported
const enum Size { Small = 1, Medium = 2, Large = 3 }
const order: Size[] = [Size.Large, Size.Small, Size.Medium];
order.sort((a, b) => a - b);
console.log(order, Size.Medium * 10);
