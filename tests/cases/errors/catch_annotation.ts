// @error: a catch variable can only be annotated as 'unknown'
try { console.log(1); } catch (e: Error) { console.log(String(e)); }
