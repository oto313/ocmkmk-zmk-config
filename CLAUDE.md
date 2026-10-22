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

BLE builds require two extra files: `config/ble_dfu.conf` (selects BLE DFU magic 0xA8) and `config/ble_split.overlay` (deletes the `wired_split` DTS node). Both must be passed via `-DEXTRA_CONF_FILE` and `-DDTC_OVERLAY_FILE`.

> **Important:** The `#if !defined(CONFIG_ZMK_BLE)` guard in `ocdmk.dtsi` does NOT work — Kconfig symbols are not available during DTS preprocessing. The `wired_split` node survives and enables `CONFIG_DT_HAS_ZMK_WIRED_SPLIT_ENABLED=y`, which breaks BLE split. The `ble_split.overlay` deletes the node properly.

**Build left half (central, BLE + USB + ZMK Studio):**
```sh
west build -s zmk/app -d build/left_ble -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_left \
  -DSNIPPET=studio-rpc-usb-uart \
  -DCONFIG_ZMK_STUDIO=y \
  -DCONFIG_ZMK_BLE=y \
  -DCONFIG_ZMK_SPLIT_WIRED=n \
  -DZMK_CONFIG=$(pwd)/config \
  -DZMK_EXTRA_MODULES=$(pwd) \
  -DEXTRA_CONF_FILE=$(pwd)/config/ble_dfu.conf \
  -DDTC_OVERLAY_FILE=$(pwd)/config/ble_split.overlay
```

**Build right half (peripheral, BLE):**
```sh
west build -s zmk/app -d build/right_ble -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_right \
  -DCONFIG_ZMK_BLE=y \
  -DCONFIG_ZMK_SPLIT_WIRED=n \
  -DZMK_CONFIG=$(pwd)/config \
  -DZMK_EXTRA_MODULES=$(pwd) \
  -DEXTRA_CONF_FILE=$(pwd)/config/ble_dfu.conf \
  -DDTC_OVERLAY_FILE=$(pwd)/config/ble_split.overlay
```

**Flash via JLink (after build):**
```sh
# Firmware only (preserves bootloader):
nrfjprog --program build/left_ble/zephyr/zmk.hex --sectorerase --verify -f NRF52 --reset

# Or using west:
west flash -d build/left_ble -r jlink
```

**Flash via UF2 (USB bootloader mode):**
Double-tap reset or press bootloader key combo (MO1 + Space on left). Board mounts as USB drive.
```sh
cp build/left_ble/zephyr/zmk.uf2 /Volumes/OCDMK_L/   # or /Volumes/BLUEMICRO/
```

### Flashing via BLE DFU

BLE builds use magic `0xA8` which enters BLE OTA DFU mode instead of USB UF2. The bootloader advertises as `ocdmk L` or `ocdmk R`.

**1. Create DFU package:**
```sh
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --sd-req 0x0100 \
  --application build/left_ble/zephyr/zmk.bin firmware_left_ble.zip
```

**2. Enter BLE DFU mode:**
- Left: press bootloader key combo (MO1 + Space)
- Right: press bootloader combo on right side (requires left to be running)
- Right has no reset button — cannot enter bootloader without left side

**3. Flash over BLE (from macOS):**
```sh
# Requires: pip install bleak
python scripts/ble_dfu_flash.py firmware_left_ble.zip "ocdmk L"
python scripts/ble_dfu_flash.py firmware_right_ble.zip "ocdmk R"
# Or auto-detect (finds any DFU device):
python scripts/ble_dfu_flash.py firmware_left_ble.zip
# Or by macOS BLE address if name is cached wrong:
python scripts/ble_dfu_flash.py firmware_left_ble.zip 997C31E1-B599-4571-C008-1E59340F054E
```

The script uses the Nordic Legacy DFU protocol over BLE via the `bleak` library. Transfer takes ~2 minutes for a full firmware image at 20 bytes/packet. macOS may cache old BLE names — use the address or auto-detect if the name doesn't match.

> **Note:** BLE DFU can be unreliable (connection drops). JLink via `nrfjprog --sectorerase` is more reliable for development. BLE DFU is useful for field updates without physical access.

### Flashing the bootloader (first time / recovery)

The board runs custom [Adafruit nRF52 Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader) builds (`ocdmk_left` / `ocdmk_right` variants based on `bluemicro_nrf52833`, S140 v7.2.0 SoftDevice). Custom bootloaders advertise as `ocdmk L` / `ocdmk R` in BLE DFU mode and mount as `OCDMK_L` / `OCDMK_R` in UF2 mode.

The bootloader hex files (merged with SoftDevice) are at:
- `/tmp/ocdmk_left_bootloader-0.11.0_s140_7.2.0.hex`
- `/tmp/ocdmk_right_bootloader-0.11.0_s140_7.2.0.hex`

**Flash bootloader + firmware (full flash via JLink):**
```sh
# Step 1: Bootloader with chip erase (erases everything)
nrfjprog --program /tmp/ocdmk_left_bootloader-0.11.0_s140_7.2.0.hex \
  --chiperase --verify -f NRF52

# Step 2: Firmware with sector erase (preserves bootloader + SoftDevice)
nrfjprog --program build/left_ble/zephyr/zmk.hex \
  --sectorerase --verify -f NRF52 --reset
```

**Erase settings partition (clear BLE bonds):**
```sh
nrfjprog --erasepage 0x6C000 -f NRF52
# ... repeat for 0x6D000 through 0x73000
nrfjprog --reset -f NRF52
```

The `build.yaml` at the repo root defines the GitHub Actions CI matrix.


## Repository Layout

This is a self-contained west workspace — ZMK and Zephyr are vendored in as subdirectories (`zmk/`, `zephyr/`), not fetched at build time. The west manifest lives at `config/west.yml` and points `self.path` to `config/`.

```
config/
  west.yml               ← west manifest
  ble_dfu.conf           ← BLE DFU magic value (0xA8) Kconfig
  ble_split.overlay      ← deletes wired_split node for BLE builds
boards/
  ocdmk/ocdmk_core/     ← custom nRF52833 board definition
  shields/ocdmk/         ← keyboard shield (left + right)
behaviors/
  src/behavior_bootloader.c ← bootloader behavior (BLE DFU / USB UF2)
drivers/
  indicator_led/         ← custom Zephyr driver for charger/battery LED
dts/bindings/            ← DTS binding for zmk,indicator-led
CMakeLists.txt           ← adds drivers/ subdirectory
Kconfig                  ← rsource drivers/Kconfig
zmk/                     ← vendored ZMK firmware (with BLE DFU Kconfig patch)
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

**BLE (pass `-DCONFIG_ZMK_BLE=y` + overlay + conf):**
- Left = central: USB to host + BLE to right half, `CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y`.
- Right = peripheral: BLE to left half, no USB.
- `wired_split` DTS node must be deleted via `config/ble_split.overlay` (`/delete-node/ &wired_split`). The `#if !defined(CONFIG_ZMK_BLE)` guard in `ocdmk.dtsi` does NOT work during DTS preprocessing.
- `config/ble_dfu.conf` selects `CONFIG_ZMK_BOOTMODE_MAGIC_VALUE_BOOTLOADER_TYPE_ADAFRUIT_NRF52_BLE` (magic 0xA8 for BLE OTA DFU instead of 0x57 USB UF2).
- `CONFIG_ZMK_BLE=y` selects the BT stack (`select BT`) and enables `CONFIG_ZMK_SPLIT_BLE=y` (depends on ZMK_BLE).

### Key matrix

7 columns × 4 rows per half, `row2col` diode direction (rows = outputs, cols = inputs with pull-down). The right side uses `col-offset = <7>` in `default_transform`. Total: 44 keys (22 per side). Row/col GPIO assignments differ between left and right due to PCB mirroring and are defined in the respective overlays.

### Custom `indicator_led` driver

Located in `drivers/indicator_led/`. A Zephyr GPIO driver (`zmk,indicator-led` compatible) that:
- Blinks LED 3× on startup
- Solid LED on while charging (BQ25185 STAT1=0 AND STAT2=0)
- Blinks LED when battery < 20% and not charging
- LED off when battery OK and not charging

Reads BQ25185 charger STAT1 (P0.00) and STAT2 (P0.31) pins. LED GPIO is overridden per-side in overlays (left: pin 11/P0.11, right: pin 1/P0.15 due to PCB mirroring).

### Custom bootloader

Custom builds of the [Adafruit nRF52 Bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader) with per-side names (`ocdmk L` / `ocdmk R`). Source repo is cloned to `../Adafruit_nRF52_Bootloader/` (sibling of this repo in the `ocdmk/` directory). Custom board definitions live at `src/boards/ocdmk_left/` and `src/boards/ocdmk_right/` (based on `bluemicro_nrf52833`, S140 v7.2.0 SoftDevice). Key customizations in `board.h`: `BLEDIS_MANUFACTURER`, `BLEDIS_MODEL`, `UF2_PRODUCT_NAME`, `UF2_VOLUME_LABEL`.

**Build bootloader (requires GNU sed, intelhex):**
```sh
cd ../Adafruit_nRF52_Bootloader
PATH="/opt/homebrew/opt/gnu-sed/libexec/gnubin:$PATH" make BOARD=ocdmk_left PYTHON=python3 all
# Output: _build/build-ocdmk_left/ocdmk_left_bootloader-0.11.0_s140_7.2.0.hex
```

**Flash bootloader + firmware via JLink (two steps):**
```sh
# Step 1: Bootloader + SoftDevice (chip erase — erases everything)
JLinkExe -device NRF52833_XXAA -if SWD -speed 4000 -autoconnect 1 -CommandFile /dev/stdin <<'EOF'
erase
loadfile ../Adafruit_nRF52_Bootloader/_build/build-ocdmk_left/ocdmk_left_bootloader-0.11.0_s140_7.2.0.hex
r
go
exit
EOF

# Step 2: ZMK firmware (sector erase — preserves bootloader + SoftDevice)
JLinkExe -device NRF52833_XXAA -if SWD -speed 4000 -autoconnect 1 -CommandFile /dev/stdin <<'EOF'
loadfile build/left_ble/zephyr/zmk.hex
r
go
exit
EOF
```
