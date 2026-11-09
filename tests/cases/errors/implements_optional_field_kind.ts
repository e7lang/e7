// @error: declare it as 'legs?: number' in the class
interface Animal { name: string; legs?: number }
class Dog implements Animal { name = "rex"; legs = 4; }
console.log(new Dog().legs);
