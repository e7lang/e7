// helper module: re-exports of shapes.ts, counter.ts and default-export.ts
export { area, PI as pi } from "./shapes.ts";
export * from "./counter.ts";
export * as shapes from "./shapes.ts";
export type { Shape } from "./shapes.ts";
export { default as hello } from "./default-export.ts";
