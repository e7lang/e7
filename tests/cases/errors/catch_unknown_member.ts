// @error: the caught value is of type 'unknown'; narrow it first
try { console.log(1); } catch (e) { console.log(e.message); }
