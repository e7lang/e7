// @error: spreading a value of abstract class type 'Named' with abstract properties is not supported
abstract class Named {
  abstract label: string;
}
class Item extends Named {
  label = "x";
}
const n: Named = new Item();
console.log({ ...n });
