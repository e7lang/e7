// third test round of 0.1.3 (catch_returns_nothing): must print what Node.js prints
async function load(): Promise<number> { throw new Error("offline"); }
load().catch((e: unknown) => { console.log("handled", String(e)); });
load().catch(() => {});
