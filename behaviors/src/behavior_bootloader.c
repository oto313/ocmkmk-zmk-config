/*
 * OCDMK Bootloader Behavior
 *
 * Writes the correct Adafruit nRF52 bootloader magic value to GPREGRET1
 * then warm-reboots into the bootloader:
 *   0x57 — USB UF2 DFU  (wired builds, CONFIG_ZMK_BLE not set)
 *   0xA8 — OTA BLE DFU  (BLE builds, CONFIG_ZMK_BLE=y)
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_bootloader

#include <zephyr/device.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/retention/retention.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    static const uint8_t magic = 0xA8; /* OTA BLE DFU */
#else
    static const uint8_t magic = 0x57; /* USB UF2 DFU */
#endif

    const struct device *gpregret = DEVICE_DT_GET(DT_CHOSEN(zmk_magic_boot_mode));
    if (!device_is_ready(gpregret)) {
        LOG_ERR("GPREGRET retention device not ready");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    int ret = retention_write(gpregret, 0, &magic, sizeof(magic));
    if (ret < 0) {
        LOG_ERR("Failed to write GPREGRET (%d)", ret);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    sys_reboot(SYS_REBOOT_WARM);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_bootloader_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_bootloader_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
