# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

This repo uses [ZMK's west build system](https://zmk.dev/docs/user-setup). Builds are run from the `zmk/app` directory inside the west workspace. Two firmware variants are supported:

### Wired split (no BLE radio, UART cable between halves)

**Build left half (central, with ZMK Studio):**
```sh
west build -s zmk/app -d build/left -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_left \
  -DSNIPPET=studio-rpc-usb-uart \
  -DCONFIG_ZMK_STUDIO=y \
  -DZMK_CONFIG=$(pwd)/config
```

**Build right half (peripheral):**
```sh
west build -s zmk/app -d build/right -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_right \
  -DZMK_CONFIG=$(pwd)/config
```

### BLE split (wireless between halves, USB to host on left)

**Build left half (central, BLE + USB + ZMK Studio):**
```sh
west build -s zmk/app -d build/left_ble -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_left \
  -DSNIPPET=studio-rpc-usb-uart \
  -DCONFIG_ZMK_STUDIO=y \
  -DCONFIG_ZMK_BLE=y \
  -DZMK_CONFIG=$(pwd)/config
```

**Build right half (peripheral, BLE):**
```sh
west build -s zmk/app -d build/right_ble -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_right \
  -DCONFIG_ZMK_BLE=y \
  -DZMK_CONFIG=$(pwd)/config
```

**Flash (after build):**
```sh
west flash -d build/left      # wired left
west flash -d build/right     # wired right
west flash -d build/left_ble  # BLE left
west flash -d build/right_ble # BLE right
```

The `build.yaml` at the repo root defines the GitHub Actions CI matrix (same two targets).

## Repository Layout

This is a self-contained west workspace — ZMK and Zephyr are vendored in as subdirectories (`zmk/`, `zephyr/`), not fetched at build time. The west manifest lives at `config/west.yml` and points `self.path` to `config/`.

```
config/west.yml          ← west manifest
boards/
  ocdmk/ocdmk_core/     ← custom nRF52833 board definition
  shields/ocdmk/         ← keyboard shield (left + right)
drivers/
  indicator_led/         ← custom Zephyr driver for charger status LED
dts/bindings/            ← DTS binding for zmk,indicator-led
CMakeLists.txt           ← adds drivers/ subdirectory
Kconfig                  ← rsource drivers/Kconfig
zmk/                     ← vendored ZMK firmware
zephyr/                  ← vendored Zephyr RTOS
```

## Architecture

### Board vs Shield split

`ocdmk_core` is the **board** (the nRF52833 PCB module). It defines hardware peripherals — PWM backlight on P0.30, UART0 for wired split, USB, battery ADC — and exposes a named **GPIO nexus** (`ocdmk_core` connector) that maps logical pin numbers 1–17 to real nRF52833 GPIO. This abstraction exists because the PCB connector is **mirrored** between left and right halves, so shield overlays reference pin numbers rather than raw GPIO.

`ocdmk` is the **shield** (the keyboard PCB). It configures the key matrix, wired split transport, charging indicator, and physical layout. Left/right differences live entirely in `ocdmk_left.overlay` and `ocdmk_right.overlay`.

### Split transport (two firmware variants)

Two mutually exclusive transport modes are supported, selected at build time via `-DCONFIG_ZMK_BLE=y`:

**Wired (default):**
- Left = central: USB to host, `CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y`, no BLE radio.
- Right = peripheral: no USB, no BLE.
- Communication: crossover UART cable — Left TX (P0.09/pin 4) ↔ Right RX (P0.05/pin 13); Left RX (P0.20/pin 3) ↔ Right TX (P0.04/pin 14).
- `wired_split` DTS node (in `ocdmk.dtsi`) auto-selects `CONFIG_ZMK_SPLIT_WIRED` via `DT_HAS_ZMK_WIRED_SPLIT_ENABLED`.
- UART0 is disabled in the board DTS; the left/right overlays enable it with per-side pinctrl inside a `#if !defined(CONFIG_ZMK_BLE)` guard.

**BLE (pass `-DCONFIG_ZMK_BLE=y`):**
- Left = central: USB to host + BLE to right half, `CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y`.
- Right = peripheral: BLE to left half, no USB.
- `wired_split` DTS node is compiled out by the `#if !defined(CONFIG_ZMK_BLE)` guard in `ocdmk.dtsi`; UART pinctrl likewise excluded from overlays.
- `CONFIG_ZMK_BLE=y` selects the BT stack (`select BT`) and enables `CONFIG_ZMK_SPLIT_BLE=y` (depends on ZMK_BLE).

### Key matrix

7 columns × 4 rows per half, `row2col` diode direction (rows = outputs, cols = inputs with pull-down). The right side uses `col-offset = <7>` in `default_transform`. Total: 44 keys (22 per side). Row/col GPIO assignments differ between left and right due to PCB mirroring and are defined in the respective overlays.

### Custom `indicator_led` driver

Located in `drivers/indicator_led/`. A minimal Zephyr GPIO driver (`zmk,indicator-led` compatible) that reads BQ25185 charger STAT1 (P0.00) and STAT2 (P0.31) pins and drives an LED output when both are high. Registered via `DEVICE_DT_INST_DEFINE`. The DTS node is in `ocdmk.dtsi` with `led-gpios` overridden per-side in the overlays.
