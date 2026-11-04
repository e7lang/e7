// @error: because it is a read-only property
class P { readonly x: number = 1; bump(): void { this.x = 3; } }
