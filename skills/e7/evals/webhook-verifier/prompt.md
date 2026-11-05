---
tags: [e7]
max_turns: 30
timeout_seconds: 900
allowed_tools: [Read, Glob, Grep, Skill, Write, Edit, Bash]
---

We use E7 scripts in our Android app (they get compiled with e7c and shipped to the app). Write me an E7 script `webhook.ts` that verifies incoming webhooks from our partner. The header looks like `t=1700000000,v1=<hex hmac>,v1=<another hex hmac>` (there can be several v1 entries during key rotation). The signature is HMAC-SHA256 over `"<t>.<raw body>"` with our shared secret. Reject anything older than 5 minutes. The app should get back whether it's valid and if not, a reason it can log. Please also add a few tests.
