// @error: may not be an optional property access
const xs: number[] | undefined = [1];
xs?.[0] = 2;
