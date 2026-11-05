---
tags: [e7]
max_turns: 30
timeout_seconds: 900
allowed_tools: [Read, Glob, Grep, Skill, Write, Edit, Bash]
---

I need an E7 function for our app's remote config. The server sends JSON like

{"flags": {"newCheckout": {"enabled": true, "rollout": 30}, "darkMode": {"enabled": false, "rollout": 100}}, "version": 12}

(flag names are arbitrary and change over time). Write `flags.ts` with an exported function the app can call with the JSON text and a user id string, that returns a comma-separated list of the flags that are on for that user. A flag is on if enabled and the user falls in the rollout percentage — bucket users deterministically by hashing flag name + user id (so the same user always gets the same answer). Make it robust against a bad config.
