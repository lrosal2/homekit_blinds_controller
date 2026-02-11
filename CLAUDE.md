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
- 28BYJ-48 stepper motor via ULN2003 driver (pins D0=GPIO0, D1=GPIO1, D2=GPIO2, D3=GPIO21)
- 2048 steps = 1 full revolution = full travel
- Matter Window Covering cluster: position in percent100ths (0-10000)

## Build System
- ESP-IDF v5.2.3 with esp-matter
- When PRIV_REQUIRES is specified in idf_component_register, ONLY those components get include paths
- `app_reset` is a component under `${ESP_MATTER_PATH}/examples/common`, not just a header

## Key Files
- `main/app_driver.cpp` — stepper wiring to position commands
- `main/app_main.cpp` — Matter device setup, stepper init
- `main/app_priv.h` — private declarations
- `components/stepper/` — stepper motor driver component

## Known Issues
- stepper_move_steps() blocks the CHIP task thread, triggers watchdog on long moves (~6s for 2048 steps)
- Future fix: move stepper to a separate FreeRTOS task
