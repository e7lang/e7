// third test round of 0.1.3 (t17_loop_tdz): must print what Node.js prints
for (let i = 0; i < 3; i++) {
  if (i > 0) { try { console.log("early", cur()); } catch (e) { console.log(String(e)); } }
  const v = i * 7;
  function cur(): number { return v; }
  console.log("late", cur());
}
