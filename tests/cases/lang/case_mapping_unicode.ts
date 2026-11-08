// toUpperCase / toLowerCase: full Unicode, with Greek final sigma
const words = ["ΟΔΟΣ", "ΟΔΟΣ ΚΑΙ", "Σ", "ΑΣ.", "Α.Σ", "ΑΣΑ", "İstanbul", "ǅ", "ﬃ", "ŉ", "ΐ", "Việt", "ș ț Ș Ț",
  "Ÿ", "µ", "ґҐ", "κόσμε", "ΆΈΉΊΌΎΏ", "𐐀𐐨", "straße", "ǰ", "ﬀ"];
for (const w of words) console.log(w.toUpperCase(), w.toLowerCase());
let changed = 0;
for (let c = 0; c <= 0xFFFF; c++) {
  if (c >= 0xD800 && c <= 0xDFFF) continue;
  const s = String.fromCharCode(c);
  if (s.toUpperCase() !== s) changed++;
  if (s.toLowerCase() !== s) changed++;
}
console.log(changed);
