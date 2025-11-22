#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* Sensor devices */
static const struct device *bmi270 = DEVICE_DT_GET(DT_NODELABEL(bmi270));
static const struct device *bmm150 = DEVICE_DT_GET(DT_NODELABEL(bmm150));

/* Custom UUIDs for IMU Service */
static const struct bt_uuid_128 imu_svc_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0));
static const struct bt_uuid_128 imu_data_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));

/* IMU data packet (36 bytes: 9 floats) */
struct imu_packet {
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float mag_x, mag_y, mag_z;
} __packed;

static struct imu_packet current_data;
static bool notify_enabled = false;
static struct bt_conn *default_conn;

/* BLE notification config changed callback */
static void imu_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("IMU notifications %s\n", notify_enabled ? "enabled" : "disabled");
    if (notify_enabled && device_is_ready(led.port)) {
        gpio_pin_set_dt(&led, 1);
    } else if (device_is_ready(led.port)) {
        gpio_pin_set_dt(&led, 0);
    }
}

/* GATT Service Definition */
BT_GATT_SERVICE_DEFINE(imu_svc,
    BT_GATT_PRIMARY_SERVICE(&imu_svc_uuid),
    BT_GATT_CHARACTERISTIC(&imu_data_uuid,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_NONE,
        NULL, NULL, &current_data),
    BT_GATT_CCC(imu_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56),
};
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err %u)\n", err);
    } else {
        printk("Connected!\n");
        default_conn = bt_conn_ref(conn);
        
        /* Request MTU exchange */
        struct bt_conn_le_data_len_param param = {
            .tx_max_len = 65,
            .tx_max_time = 2120,
        };
        bt_conn_le_data_len_update(conn, &param);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    printk("Disconnected (reason %u)\n", reason);
    notify_enabled = false;
    if (device_is_ready(led.port)) {
        gpio_pin_set_dt(&led, 0);
    }
    if (default_conn) {
        bt_conn_unref(default_conn);
        default_conn = NULL;
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

void main(void)
{
    int err;
    printk("\n\n=== BLE IMU Streamer v1.2 (Full Data) ===\n");
    printk("Board: Arduino Nano 33 BLE Rev2\n");
    printk("Sample Rate: 50Hz\n");

    if (device_is_ready(led.port)) {
        gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    }

    /* Sensors init */
    if (device_is_ready(bmi270)) {
        printk("BMI270 ready\n");
    }
    if (device_is_ready(bmm150)) {
        printk("BMM150 ready\n");
    }

    err = bt_enable(NULL);
    if (err) {
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        return;
    }

    struct sensor_value accel[3], gyro[3], mag[3];
    int64_t next_time = k_uptime_get();
    const int interval_ms = 20; // 50Hz
    uint32_t sample_count = 0;

    while (1) {
        /* Read Sensors */
        if (device_is_ready(bmi270)) {
            sensor_sample_fetch(bmi270);
            sensor_channel_get(bmi270, SENSOR_CHAN_ACCEL_XYZ, accel);
            sensor_channel_get(bmi270, SENSOR_CHAN_GYRO_XYZ, gyro);
            
            current_data.accel_x = sensor_value_to_double(&accel[0]);
            current_data.accel_y = sensor_value_to_double(&accel[1]);
            current_data.accel_z = sensor_value_to_double(&accel[2]);
            current_data.gyro_x = sensor_value_to_double(&gyro[0]);
            current_data.gyro_y = sensor_value_to_double(&gyro[1]);
            current_data.gyro_z = sensor_value_to_double(&gyro[2]);
        }

        if (device_is_ready(bmm150)) {
            sensor_sample_fetch(bmm150);
            sensor_channel_get(bmm150, SENSOR_CHAN_MAGN_XYZ, mag);
            
            current_data.mag_x = sensor_value_to_double(&mag[0]);
            current_data.mag_y = sensor_value_to_double(&mag[1]);
            current_data.mag_z = sensor_value_to_double(&mag[2]);
        }

        /* Send Notification */
        if (notify_enabled && default_conn) {
            err = bt_gatt_notify(default_conn, &imu_svc.attrs[2], &current_data, sizeof(current_data));
        }

        /* LED Logic: Solid ON if notifying */
        if (device_is_ready(led.port)) {
            if (notify_enabled) {
                gpio_pin_set_dt(&led, 1);
            } else {
                 /* Blink slowly if waiting */
                if (sample_count % 25 == 0) {
                    gpio_pin_toggle_dt(&led);
                }
            }
        }

        sample_count++;
        next_time += interval_ms;
        int64_t sleep = next_time - k_uptime_get();
        if (sleep > 0) {
            k_msleep(sleep);
        }
    }
}