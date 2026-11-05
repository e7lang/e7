// @feature: The `satisfies` operator
// @category: Everyday Types
// @status: supported
// @note: the expression keeps its own type
interface Config { host: string; port: number }
const cfg = { host: "localhost", port: 8080 } satisfies Config;
console.log(cfg.host, cfg.port + 1);
