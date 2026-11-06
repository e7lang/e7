// side-effect import of order_c before its own initialization
import "./order_c.ts";
import { record } from "./order_log.ts";
record("a");
