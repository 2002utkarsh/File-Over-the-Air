/*
 * FOTA Update System - Embedded Device Firmware
 * 
 * Architecture:
 *   AWS Cloud ← Internet → Embedded Linux (Gateway) ← UART → This Device
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_GPIO
    #include <zephyr/drivers/gpio.h>
#endif

LOG_MODULE_REGISTER(fota, LOG_LEVEL_INF);

#define FIRMWARE_VERSION "1.0.0"
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__

#ifdef CONFIG_GPIO
    #define LED0_NODE DT_ALIAS(led0)
    static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
    #define LED_AVAILABLE true
#else
    #define LED_AVAILABLE false
#endif

void print_version_info() {
    printk("\n");
    printk("========================================\n");
    printk("  FOTA Update System - Device Firmware\n");
    printk("========================================\n");
    printk("Version:    %s\n", FIRMWARE_VERSION);
    printk("Build Date: %s\n", BUILD_DATE);
    printk("Build Time: %s\n", BUILD_TIME);
    printk("Platform:   %s\n", CONFIG_BOARD);
    printk("========================================\n");
    printk("\n");
}

void print_help() {
    printk("\nAvailable Commands:\n");
    printk("  version  - Display firmware version\n");
    printk("  help     - Display this help message\n");
    printk("  update   - Enter update mode (future)\n");
    printk("  status   - Display device status\n");
    printk("\n");
}

void print_status() {
    printk("\nDevice Status:\n");
    printk("  Running:  Yes\n");
    printk("  Version:  %s\n", FIRMWARE_VERSION);
    #if LED_AVAILABLE
        printk("  LED:      Active (heartbeat)\n");
    #else
        printk("  LED:      N/A (native_sim)\n");
    #endif
    printk("  Uptime:   %lld ms\n", k_uptime_get());
    printk("\n");
}

int main(void) {
    LOG_INF("===========================================");
    LOG_INF("FOTA Update System - Starting");
    LOG_INF("Version: %s", FIRMWARE_VERSION);
    LOG_INF("===========================================");

    // Initialize LED if available
    #if LED_AVAILABLE
        if (gpio_is_ready_dt(&led)) {
            int ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
            if (ret == 0) {
                LOG_INF("LED heartbeat initialized");
            } else {
                LOG_ERR("Failed to configure LED (error %d)", ret);
            }
        } else {
            LOG_WRN("LED not ready");
        }
    #else
        LOG_INF("Running on native_sim - LED disabled");
    #endif

    // Print version information at startup
    print_version_info();
    print_help();

    // Main loop - LED heartbeat and keep application running
    uint32_t heartbeat_counter = 0;
    bool led_state = false;

    while (1) {
        // LED heartbeat - blink every second
        #if LED_AVAILABLE
            if (gpio_is_ready_dt(&led)) {
                gpio_pin_set_dt(&led, led_state);
                led_state = !led_state;
            }
        #endif

        if (heartbeat_counter % 10 == 0) {
            LOG_INF("Uptime: %lld s", k_uptime_get() / 1000);
        }

        k_msleep(1000);
        heartbeat_counter++;
    }

    return 0;
}