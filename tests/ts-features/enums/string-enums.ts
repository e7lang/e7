// @feature: String enums
// @category: Enums
// @status: supported
enum Level { Debug = "DEBUG", Info = "INFO", Error = "ERROR" }
function log(level: Level, msg: string): string {
  return `[${level}] ${msg}`;
}
const all: Level[] = [Level.Debug, Level.Info, Level.Error];
console.log(log(Level.Info, "started"), all, Level.Error === "ERROR");
