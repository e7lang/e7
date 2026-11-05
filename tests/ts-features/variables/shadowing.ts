// @feature: Shadowing
// @category: Variable Declaration
// @status: supported
function sumMatrix(matrix: number[][]): number {
  let sum = 0;
  for (let i = 0; i < matrix.length; i++) {
    const currentRow = matrix[i];
    for (let i = 0; i < currentRow.length; i++) sum += currentRow[i];
  }
  return sum;
}
console.log(sumMatrix([[1, 2], [3, 4]]));
