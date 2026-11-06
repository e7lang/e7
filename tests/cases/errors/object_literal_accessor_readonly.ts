// @error: cannot assign to 'fahrenheit' because it is a read-only property
const temp = {
  celsius: 20,
  get fahrenheit(): number {
    return this.celsius * 1.8 + 32;
  },
};
temp.fahrenheit = 100;
