/*
 * FOTA Update System - Embedded Device Firmware
 * 
 * Architecture:
 *   AWS Cloud ← Internet → Embedded Linux (Gateway) ← UART → This Device
 *
 * Features:
 *   - MCUboot A/B image swap with power-loss recovery
 *   - Zephyr Shell for UART command interface
 *   - LED heartbeat for visual status
 *   - Version tracking with build metadata
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

#ifdef CONFIG_BOOTLOADER_MCUBOOT
    #include <zephyr/dfu/mcuboot.h>
#endif

#ifdef CONFIG_GPIO
    #include <zephyr/drivers/gpio.h>
#endif

LOG_MODULE_REGISTER(fota, LOG_LEVEL_INF);

/* ── Version Information ──────────────────────────────────────────────── */

#define FIRMWARE_VERSION       "1.0.0"
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 0
#define BUILD_DATE             __DATE__
#define BUILD_TIME             __TIME__

/* ── LED Heartbeat ────────────────────────────────────────────────────── */

#ifdef CONFIG_GPIO
    #define LED0_NODE DT_ALIAS(led0)
    static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
    #define LED_AVAILABLE true
#else
    #define LED_AVAILABLE false
#endif

/* ── MCUboot Image Confirmation ───────────────────────────────────────── */

#ifdef CONFIG_BOOTLOADER_MCUBOOT
/**
 * Confirm the current image so MCUboot does not revert on next reboot.
 * This implements power-loss recovery: if the device loses power before
 * confirmation, MCUboot automatically rolls back to the previous image.
 */
static int confirm_image(void)
{
    if (!boot_is_img_confirmed()) {
        int rc = boot_write_img_confirmed();
        if (rc == 0) {
            LOG_INF("Image confirmed — MCUboot will keep this firmware");
        } else {
            LOG_ERR("Failed to confirm image (error %d)", rc);
            LOG_ERR("MCUboot will revert to previous image on next reboot!");
            return rc;
        }
    } else {
        LOG_INF("Image already confirmed");
    }
    return 0;
}
#endif /* CONFIG_BOOTLOADER_MCUBOOT */

/* ── Shell Commands ───────────────────────────────────────────────────── */

/**
 * Shell command: version
 * Returns firmware version in machine-parseable format for the gateway.
 * Output format: VERSION:<major>.<minor>.<patch>
 */
static int cmd_version(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "VERSION:%s", FIRMWARE_VERSION);
    shell_print(sh, "Build:   %s %s", BUILD_DATE, BUILD_TIME);
    shell_print(sh, "Board:   %s", CONFIG_BOARD);
    return 0;
}

/**
 * Shell command: status
 * Returns device status including MCUboot image state.
 */
static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== Device Status ===");
    shell_print(sh, "Version:   %s", FIRMWARE_VERSION);
    #ifdef CONFIG_BOOTLOADER_MCUBOOT
        shell_print(sh, "Confirmed: %s", boot_is_img_confirmed() ? "yes" : "no");
    #else
        shell_print(sh, "Confirmed: N/A (no MCUboot)");
    #endif
    shell_print(sh, "Uptime:    %lld s", k_uptime_get() / 1000);
    #if LED_AVAILABLE
        shell_print(sh, "LED:       active (heartbeat)");
    #else
        shell_print(sh, "LED:       N/A (native_sim)");
    #endif
    return 0;
}

/**
 * Shell command: update confirm
 * Manually confirm the current image (prevents MCUboot rollback).
 */
static int cmd_update_confirm(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    #ifdef CONFIG_BOOTLOADER_MCUBOOT
        int rc = boot_write_img_confirmed();
        if (rc == 0) {
            shell_print(sh, "Image confirmed successfully");
        } else {
            shell_print(sh, "Failed to confirm image (error %d)", rc);
        }
        return rc;
    #else
        shell_print(sh, "MCUboot not available on this platform");
        return -ENOTSUP;
    #endif
}

/**
 * Shell command: update reboot
 * Reboot the device. If a new image is pending in the secondary slot,
 * MCUboot will swap it in during the next boot.
 */
static int cmd_update_reboot(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Rebooting to apply pending update...");
    k_msleep(500);  /* Give shell time to flush */
    sys_reboot(SYS_REBOOT_COLD);
    return 0;  /* Unreachable */
}

/* Register shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_update,
    SHELL_CMD(confirm, NULL, "Confirm the current image", cmd_update_confirm),
    SHELL_CMD(reboot,  NULL, "Reboot to apply pending update", cmd_update_reboot),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(version, NULL, "Display firmware version", cmd_version);
SHELL_CMD_REGISTER(status,  NULL, "Display device status", cmd_status);
SHELL_CMD_REGISTER(update,  &sub_update, "Update management commands", NULL);

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    LOG_INF("===========================================");
    LOG_INF("FOTA Update System - Starting");
    LOG_INF("Version: %s", FIRMWARE_VERSION);
    LOG_INF("===========================================");

    /* Confirm the running image so MCUboot keeps it.
     * If this is the first boot after an A/B swap, this prevents
     * automatic rollback on the next reboot. */
    #ifdef CONFIG_BOOTLOADER_MCUBOOT
        confirm_image();
    #else
        LOG_INF("MCUboot not enabled — skipping image confirmation");
    #endif

    /* Initialize LED if available */
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

    /* Main loop - LED heartbeat */
    uint32_t heartbeat_counter = 0;
    bool led_state = false;

    while (1) {
        #if LED_AVAILABLE
            if (gpio_is_ready_dt(&led)) {
                gpio_pin_set_dt(&led, led_state);
                led_state = !led_state;
            }
        #endif

        if (heartbeat_counter % 30 == 0) {
            LOG_INF("Heartbeat — uptime: %lld s", k_uptime_get() / 1000);
        }

        k_msleep(1000);
        heartbeat_counter++;
    }

    return 0;
}