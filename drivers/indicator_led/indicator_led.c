/*
 * Copyright (c) 2024 OCDMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Indicator LED Driver
 * Lights LED when STAT1=1 AND STAT2=1
 */

#define DT_DRV_COMPAT zmk_indicator_led

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(indicator_led, CONFIG_INDICATOR_LED_LOG_LEVEL);

struct indicator_led_config {
    struct gpio_dt_spec led_gpio;
    struct gpio_dt_spec stat1_gpio;
    struct gpio_dt_spec stat2_gpio;
};

struct indicator_led_data {
    const struct indicator_led_config *config;
    struct gpio_callback stat1_cb;
    struct gpio_callback stat2_cb;
};

static void update_led_state(const struct indicator_led_config *config)
{
    int stat1 = gpio_pin_get_dt(&config->stat1_gpio);
    int stat2 = gpio_pin_get_dt(&config->stat2_gpio);
    
    if (stat1 < 0 || stat2 < 0) {
        LOG_ERR("Failed to read STAT pins");
        return;
    }
    
    /* LED on when both STAT1=1 AND STAT2=1 */
    int led_state = (stat1 == 1 && stat2 == 1) ? 1 : 0;
    
    gpio_pin_set_dt(&config->led_gpio, led_state);
    LOG_DBG("STAT1=%d, STAT2=%d -> LED=%d", stat1, stat2, led_state);
}

static void stat1_gpio_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    struct indicator_led_data *data = CONTAINER_OF(cb, struct indicator_led_data, stat1_cb);
    update_led_state(data->config);
}

static void stat2_gpio_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    struct indicator_led_data *data = CONTAINER_OF(cb, struct indicator_led_data, stat2_cb);
    update_led_state(data->config);
}

static int indicator_led_init(const struct device *dev)
{
    const struct indicator_led_config *config = dev->config;
    struct indicator_led_data *data = dev->data;
    int ret;
    
    data->config = config;
    
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
    
    /* Set initial LED state */
    update_led_state(config);
    
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
