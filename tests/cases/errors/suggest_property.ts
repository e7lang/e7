// @error: property 'widht' does not exist on type 'Box'; did you mean 'width'?
class Box { width = 1; height = 2; }
console.log(new Box().widht);
