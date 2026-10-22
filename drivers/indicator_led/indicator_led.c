/*
 * Copyright (c) 2024 OCDMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Indicator LED Driver
 * - Blinks LED on startup
 * - Solid LED on while charging
 * - Blinks LED when battery < 20% and not charging
 */

#define DT_DRV_COMPAT zmk_indicator_led

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
#include <zephyr/drivers/sensor.h>
#endif
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(indicator_led, CONFIG_INDICATOR_LED_LOG_LEVEL);

#define BATTERY_CHECK_INTERVAL_S 60
#define BLINK_INTERVAL_MS        500
#define LOW_BATTERY_THRESHOLD    20
#define STARTUP_BLINKS           3
#define STARTUP_BLINK_MS         100

struct indicator_led_config {
    struct gpio_dt_spec led_gpio;
    struct gpio_dt_spec stat1_gpio;
    struct gpio_dt_spec stat2_gpio;
};

struct indicator_led_data {
    const struct indicator_led_config *config;
    struct gpio_callback stat1_cb;
    struct gpio_callback stat2_cb;
    struct k_work_delayable battery_work;
    bool blink_state;
};

/* Read battery state of charge via sensor API (requires battery reporting) */
#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static const struct device *const battery_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_battery));

static int get_battery_pct(void)
{
    if (!device_is_ready(battery_dev)) {
        return -1;
    }

    int rc = sensor_sample_fetch_chan(battery_dev, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE);
    if (rc != 0) {
        LOG_DBG("Failed to fetch battery: %d", rc);
        return -1;
    }

    struct sensor_value soc;
    rc = sensor_channel_get(battery_dev, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, &soc);
    if (rc != 0) {
        LOG_DBG("Failed to get battery SoC: %d", rc);
        return -1;
    }

    return soc.val1;
}
#else
static int get_battery_pct(void)
{
    return -1;
}
#endif

static bool is_charging(const struct indicator_led_config *config)
{
    int stat1 = gpio_pin_get_dt(&config->stat1_gpio);
    int stat2 = gpio_pin_get_dt(&config->stat2_gpio);

    if (stat1 < 0 || stat2 < 0) {
        LOG_ERR("Failed to read STAT pins");
        return false;
    }

    /* BQ25185: STAT1=0 AND STAT2=0 means charging in progress */
    return (stat1 == 0 && stat2 == 0);
}

static void battery_check_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct indicator_led_data *data = CONTAINER_OF(dwork, struct indicator_led_data, battery_work);
    const struct indicator_led_config *config = data->config;

    int pct = get_battery_pct();
    bool charging = is_charging(config);

    LOG_DBG("Battery: %d%%, charging: %d", pct, charging);

    if (charging) {
        /* Charging: LED solid on */
        LOG_INF("LED ON: charging (battery %d%%)", pct);
        data->blink_state = false;
        gpio_pin_set_dt(&config->led_gpio, 1);
        k_work_reschedule(&data->battery_work, K_SECONDS(BATTERY_CHECK_INTERVAL_S));
    } else if (pct >= 0 && pct < LOW_BATTERY_THRESHOLD) {
        /* Low battery and not charging: blink */
        LOG_INF("LED BLINK: low battery (%d%%)", pct);
        data->blink_state = !data->blink_state;
        gpio_pin_set_dt(&config->led_gpio, data->blink_state);
        k_work_reschedule(&data->battery_work, K_MSEC(BLINK_INTERVAL_MS));
    } else {
        /* Battery OK, not charging: LED off, check again later */
        LOG_INF("LED OFF: not charging, battery %d%%", pct);
        data->blink_state = false;
        gpio_pin_set_dt(&config->led_gpio, 0);
        k_work_reschedule(&data->battery_work, K_SECONDS(BATTERY_CHECK_INTERVAL_S));
    }
}

static void stat1_gpio_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    struct indicator_led_data *data = CONTAINER_OF(cb, struct indicator_led_data, stat1_cb);
    /* Charger state changed — re-evaluate immediately */
    data->blink_state = false;
    k_work_reschedule(&data->battery_work, K_NO_WAIT);
}

static void stat2_gpio_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    struct indicator_led_data *data = CONTAINER_OF(cb, struct indicator_led_data, stat2_cb);
    data->blink_state = false;
    k_work_reschedule(&data->battery_work, K_NO_WAIT);
}

static int indicator_led_init(const struct device *dev)
{
    const struct indicator_led_config *config = dev->config;
    struct indicator_led_data *data = dev->data;
    int ret;

    data->config = config;
    data->blink_state = false;

    /* Configure LED GPIO as output */
    if (!gpio_is_ready_dt(&config->led_gpio)) {
        LOG_ERR("LED GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->led_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure LED GPIO: %d", ret);
        return ret;
    }

    /* Startup blink */
    for (int i = 0; i < STARTUP_BLINKS; i++) {
        gpio_pin_set_dt(&config->led_gpio, 1);
        k_msleep(STARTUP_BLINK_MS);
        gpio_pin_set_dt(&config->led_gpio, 0);
        k_msleep(STARTUP_BLINK_MS);
    }

    /* Configure STAT1 GPIO as input with interrupt */
    if (!gpio_is_ready_dt(&config->stat1_gpio)) {
        LOG_ERR("STAT1 GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->stat1_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure STAT1 GPIO: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&config->stat1_gpio, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        LOG_ERR("Failed to configure STAT1 interrupt: %d", ret);
        return ret;
    }

    /* Configure STAT2 GPIO as input with interrupt */
    if (!gpio_is_ready_dt(&config->stat2_gpio)) {
        LOG_ERR("STAT2 GPIO not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&config->stat2_gpio, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure STAT2 GPIO: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure_dt(&config->stat2_gpio, GPIO_INT_EDGE_BOTH);
    if (ret < 0) {
        LOG_ERR("Failed to configure STAT2 interrupt: %d", ret);
        return ret;
    }

    /* Setup GPIO callbacks */
    gpio_init_callback(&data->stat1_cb, stat1_gpio_callback, BIT(config->stat1_gpio.pin));
    gpio_add_callback(config->stat1_gpio.port, &data->stat1_cb);

    gpio_init_callback(&data->stat2_cb, stat2_gpio_callback, BIT(config->stat2_gpio.pin));
    gpio_add_callback(config->stat2_gpio.port, &data->stat2_cb);

    /* Initialize periodic battery check (delay to let sensor init) */
    k_work_init_delayable(&data->battery_work, battery_check_handler);
    k_work_reschedule(&data->battery_work, K_SECONDS(5));

    LOG_INF("Indicator LED initialized");
    return 0;
}

#define INDICATOR_LED_INIT(n)                                                   \
    static struct indicator_led_data indicator_led_data_##n;                    \
                                                                                \
    static const struct indicator_led_config indicator_led_config_##n = {       \
        .led_gpio = GPIO_DT_SPEC_INST_GET(n, led_gpios),                        \
        .stat1_gpio = GPIO_DT_SPEC_INST_GET(n, stat1_gpios),                    \
        .stat2_gpio = GPIO_DT_SPEC_INST_GET(n, stat2_gpios),                    \
    };                                                                          \
                                                                                \
    DEVICE_DT_INST_DEFINE(n, indicator_led_init, NULL,                          \
                          &indicator_led_data_##n, &indicator_led_config_##n,   \
                          POST_KERNEL, CONFIG_INDICATOR_LED_INIT_PRIORITY,      \
                          NULL);

DT_INST_FOREACH_STATUS_OKAY(INDICATOR_LED_INIT)
