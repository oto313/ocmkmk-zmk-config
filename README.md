# OCDMK ZMK Config

ZMK firmware for the OCDMK — a custom wired/BLE split keyboard based on a nRF52833 core module.

## Hardware

- **MCU**: Nordic nRF52833 (custom `ocdmk_core` board)
- **Layout**: 44 keys, 7 columns × 4 rows per half
- **Split**: Wired (UART crossover cable) or BLE (wireless)
- **Bootloader**: Adafruit nRF52 (UF2 drag-and-drop flashing)

## Flashing the Bootloader (first time only)

Download the latest hex from [Adafruit nRF52 Bootloader releases](https://github.com/adafruit/Adafruit_nRF52_Bootloader/releases) — pick `bluemicro_nrf52833_bootloader-*_s140_7.2.0.hex`.

**With nrfjprog:**
```sh
nrfjprog --program bluemicro_nrf52833_bootloader-0.10.0_s140_7.2.0.hex \
  --chiperase --verify -f NRF52 --reset
```

**With JLink:**
```sh
JLinkExe -device nRF52833_xxAA -if SWD -speed 4000 -autoconnect 1 << 'EOF'
h
erase
loadfile bluemicro_nrf52833_bootloader-0.10.0_s140_7.2.0.hex
r
g
exit
EOF
```

After flashing, double-tap the reset button — the board mounts as a USB drive. Subsequent firmware updates use `west flash` (UF2).

## Building

Builds are run from the repo root using ZMK's west build system.

### Wired split (UART cable between halves)

```sh
# Left half (central, USB to host, ZMK Studio)
west build -s zmk/app -d build/left -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_left \
  -DSNIPPET=studio-rpc-usb-uart \
  -DCONFIG_ZMK_STUDIO=y \
  -DZMK_CONFIG=$(pwd)/config

# Right half (peripheral)
west build -s zmk/app -d build/right -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_right \
  -DZMK_CONFIG=$(pwd)/config
```

### BLE split (wireless between halves)

```sh
# Left half
west build -s zmk/app -d build/left_ble -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_left \
  -DSNIPPET=studio-rpc-usb-uart \
  -DCONFIG_ZMK_STUDIO=y \
  -DCONFIG_ZMK_BLE=y \

  -DZMK_CONFIG=$(pwd)/config

# Right half
west build -s zmk/app -d build/right_ble -b ocdmk_core/nrf52833 -- \
  -DSHIELD=ocdmk_right \
  -DCONFIG_ZMK_BLE=y \

  -DZMK_CONFIG=$(pwd)/config
```

## Flashing Firmware

Double-tap reset to enter UF2 bootloader mode, then:

```sh
west flash -d build/left      # wired left
west flash -d build/right     # wired right
west flash -d build/left_ble  # BLE left
west flash -d build/right_ble # BLE right
```
