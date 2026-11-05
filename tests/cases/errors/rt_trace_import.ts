// @runtime-error: at explode (_lib/thrower.ts:3:3)
// @runtime-error: at <init> (rt_trace_import.ts:4:1)
import { explode } from "./_lib/thrower.ts";
explode("boom");
