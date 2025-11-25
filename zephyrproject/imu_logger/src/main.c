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


/* Buffering Configuration */

#define BUFFER_DURATION_SEC 5

#define SAMPLE_RATE_HZ 50

#define BUFFER_SIZE (BUFFER_DURATION_SEC * SAMPLE_RATE_HZ)
#define SAMPLES_PER_PACKET 6

#define BATCH_PACKET_SIZE (SAMPLES_PER_PACKET * sizeof(struct imu_packet))




enum app_state {

    STATE_RECORDING,

    STATE_SENDING,

};



static struct imu_packet imu_buffer[BUFFER_SIZE];

static uint16_t buffer_index = 0;

static enum app_state current_state = STATE_RECORDING;


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
    BT_GATT_CCC(imu_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),

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


/* Security callback */

static void security_changed(struct bt_conn *conn, bt_security_t level,

                             enum bt_security_err err)

{

    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));



    if (!err) {

        printk("Security changed: %s level %u\\n", addr, level);

    } else {

        printk("Security failed: %s level %u err %d\\n", addr, level, err);

    }

}



/* Authentication info callbacks */

static void pairing_complete(struct bt_conn *conn, bool bonded)

{

    printk("Pairing Complete (bonded: %d)\\n", bonded);

}



static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)

{

    printk("Pairing Failed (reason %d)\\n", reason);

}



/* Empty auth callbacks to enable Just Works pairing */

static const struct bt_conn_auth_cb auth_callbacks = {

    .pairing_confirm = NULL,

    .cancel = NULL,

};



static struct bt_conn_auth_info_cb auth_info_callbacks = {

    .pairing_complete = pairing_complete,

    .pairing_failed = pairing_failed,

};



/* Connection callbacks */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printk("Connection failed (err %u)\n", err);
    } else {
        printk("Connected!\n");
        default_conn = bt_conn_ref(conn);
        
        /* Request Data Length Update */
        struct bt_conn_le_data_len_param param = {
            .tx_max_len = 251,

            .tx_max_time = 2120,
        };
        bt_conn_le_data_len_update(conn, &param);
        

        /* Request fast connection interval (7.5ms) */

        struct bt_le_conn_param *param_conn = BT_LE_CONN_PARAM(6, 6, 0, 400);

        bt_conn_le_param_update(conn, param_conn);

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
    .security_changed = security_changed,

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







    /* Register authentication info callbacks */

    /* Register auth callbacks to enable pairing */

    bt_conn_auth_cb_register(&auth_callbacks);

    bt_conn_auth_info_cb_register(&auth_info_callbacks);

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

    const int interval_ms = 1000 / SAMPLE_RATE_HZ;

    uint32_t sample_count = 0;



    while (1) {

        if (current_state == STATE_RECORDING) {

            /* LED: Blink slowly (1Hz) */

            if (device_is_ready(led.port)) {

                if (sample_count % (SAMPLE_RATE_HZ / 2) == 0) {

                    gpio_pin_toggle_dt(&led);

                }

            }



            /* Read Sensors */

            if (device_is_ready(bmi270)) {

                sensor_sample_fetch(bmi270);

                sensor_channel_get(bmi270, SENSOR_CHAN_ACCEL_XYZ, accel);

                sensor_channel_get(bmi270, SENSOR_CHAN_GYRO_XYZ, gyro);

                

                imu_buffer[buffer_index].accel_x = sensor_value_to_double(&accel[0]);

                imu_buffer[buffer_index].accel_y = sensor_value_to_double(&accel[1]);

                imu_buffer[buffer_index].accel_z = sensor_value_to_double(&accel[2]);

                imu_buffer[buffer_index].gyro_x = sensor_value_to_double(&gyro[0]);

                imu_buffer[buffer_index].gyro_y = sensor_value_to_double(&gyro[1]);

                imu_buffer[buffer_index].gyro_z = sensor_value_to_double(&gyro[2]);

            }



            if (device_is_ready(bmm150)) {

                sensor_sample_fetch(bmm150);

                sensor_channel_get(bmm150, SENSOR_CHAN_MAGN_XYZ, mag);

                

                imu_buffer[buffer_index].mag_x = sensor_value_to_double(&mag[0]);

                imu_buffer[buffer_index].mag_y = sensor_value_to_double(&mag[1]);

                imu_buffer[buffer_index].mag_z = sensor_value_to_double(&mag[2]);

            }



            buffer_index++;

            if (buffer_index >= BUFFER_SIZE) {

                printk("Buffer full (%d samples). Switching to SENDING state.\n", buffer_index);

                current_state = STATE_SENDING;

                /* LED Solid ON during transmission */

                if (device_is_ready(led.port)) {

                    gpio_pin_set_dt(&led, 1);

                }

            }



            /* Timing control */

            sample_count++;

            next_time += interval_ms;

            int64_t sleep = next_time - k_uptime_get();

            if (sleep > 0) {

                k_msleep(sleep);

            }



        } else if (current_state == STATE_SENDING) {

            if (notify_enabled && default_conn) {

                printk("Sending %d buffered samples in packed batches...\n", buffer_index);

                

                /* Temporary buffer for packed data */

                uint8_t batch_buffer[BATCH_PACKET_SIZE];

                

                for (int i = 0; i < buffer_index; i += SAMPLES_PER_PACKET) {

                    /* Calculate how many samples to pack (handle last partial batch) */

                    int samples_to_pack = SAMPLES_PER_PACKET;

                    if (i + samples_to_pack > buffer_index) {

                        samples_to_pack = buffer_index - i;

                    }

                    

                    /* Pack data */

                    memcpy(batch_buffer, &imu_buffer[i], samples_to_pack * sizeof(struct imu_packet));

                    

                    /* Send notification */

                    int err = bt_gatt_notify(default_conn, &imu_svc.attrs[2], batch_buffer, samples_to_pack * sizeof(struct imu_packet));

                    if (err) {

                        printk("Notify failed (err %d)\n", err);

                        k_msleep(10); /* Retry delay */

                        i -= SAMPLES_PER_PACKET; /* Retry this batch */

                    }

                }

                printk("All sent. Clearing buffer.\n");

            } else {

                printk("Connection lost or notifications disabled. Dropping data.\n");

            }



            /* Reset buffer and state */

            buffer_index = 0;

            current_state = STATE_RECORDING;

            sample_count = 0;

            next_time = k_uptime_get(); /* Reset timing */

        }

    }

}

