# HomeKit Blinds Controller

## Workflow Rules
- ALWAYS fetch/sync with remote before making any changes
- NEVER give ad-hoc git commands for the user to run on their machine
- You edit, commit, push. User pulls. That's the workflow.
- Verify your changes compile/make sense before pushing
- One deliberate step at a time. No speculative fixes.
- If you're unsure, say so. Don't guess.
- Verify before you act. Read state before modifying it.

## Project Overview
- ESP32-C6 based window covering controller using ESP-IDF + esp-matter
- Currently at: Light example over Thread (foundation step)
- Next: Swap to window covering once Thread pairing is confirmed working

## Build System
- ESP-IDF v5.2.3 with esp-matter
- Target: Seeed XIAO ESP32-C6
- sdkconfig.defaults.esp32c6 contains Thread config (based on official esp-matter c6_thread)
- Build: delete build/ and sdkconfig first for clean rebuild, then `idf.py set-target esp32c6 build`

## Key Files
- `main/app_driver.cpp` — light driver (stock esp-matter example)
- `main/app_main.cpp` — Matter device setup (stock esp-matter light example)
- `main/app_priv.h` — private declarations + OpenThread config macros
- `sdkconfig.defaults.esp32c6` — Thread config (official esp-matter c6_thread reference)
