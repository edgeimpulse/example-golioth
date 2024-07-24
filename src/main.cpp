/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zcbor_encode.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"
#include "model_metadata.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ei_glth, LOG_LEVEL_DBG);

#include <golioth/client.h>
#include <golioth/stream.h>
#include <samples/common/sample_credentials.h>
#include <string.h>

#include <samples/common/net_connect.h>

struct golioth_client *client;
static K_SEM_DEFINE(connected, 0, 1);

static void on_client_event(struct golioth_client *client,
                            enum golioth_client_event event,
                            void *arg)
{
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    if (is_connected)
    {
        k_sem_give(&connected);
    }
    GLTH_LOGI(TAG, "Golioth client %s", is_connected ? "connected" : "disconnected");
}

#define SW_0 DT_ALIAS(sw0)
static const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET_OR(SW_0, gpios, {0});
static struct gpio_callback btn_cb_data;
static K_SEM_DEFINE(btn_press, 0, 1);

void btn_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    GLTH_LOGD(TAG, "Button pressed.");
    k_sem_give(&btn_press);
}

const struct device *accel = DEVICE_DT_GET_ONE(adi_adxl362);

static float readings[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

int sample_accel(size_t offset, size_t length, float *out_ptr)
{
    if (length > EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE)
    {
        GLTH_LOGE(TAG, "Unexpected features length: %d", length);
        return 1;
    }
    GLTH_LOGD(TAG, "Collecting %u samples.", length);
    size_t i = offset;
    while (i - offset < length)
    {
        int err = sensor_sample_fetch(accel);
        if (err)
        {
            GLTH_LOGE(TAG, "Error fetching accelerometer data: %d", err);
            return 1;
        }
        struct sensor_value accel_x;
        struct sensor_value accel_y;
        struct sensor_value accel_z;
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &accel_x);
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &accel_y);
        sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &accel_z);
        readings[i] = sensor_value_to_float(&accel_x);
        readings[i + 1] = sensor_value_to_float(&accel_y);
        readings[i + 2] = sensor_value_to_float(&accel_z);
        i += 3;
        /* Sample at 62.5 Hz */
        k_sleep(K_MSEC(16));
    }
    memcpy(out_ptr, readings + offset, length * sizeof(float));
    return 0;
}

enum golioth_status upload_accel_readings(uint32_t block_idx,
                                          uint8_t *block_buffer,
                                          size_t *block_size,
                                          bool *is_last,
                                          void *arg)
{
    uint32_t offset = block_idx * *block_size;
    uint32_t remaining = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * sizeof(float) - offset;
    if (remaining <= *block_size)
    {
        *block_size = remaining;
        *is_last = true;
    }

    GLTH_LOGI(TAG, "Uploading accelerometer readings [idx: %u] [rem: %u]", block_idx, remaining);
    memcpy(block_buffer, (uint8_t *) readings + offset, *block_size);
    return GOLIOTH_OK;
}

int main()
{
    int err;

    GLTH_LOGD(TAG, "Starting Edge Impulse Golioth example...");

    net_connect();

    /* Note: In production, you would provision unique credentials onto each
     * device. For simplicity, we provide a utility to hardcode credentials as
     * kconfig options in the samples.
     */
    const struct golioth_client_config *client_config = golioth_sample_credentials_get();

    client = golioth_client_create(client_config);

    golioth_client_register_event_callback(client, on_client_event, NULL);

    k_sem_take(&connected, K_FOREVER);

    GLTH_LOGD(TAG, "Configuring button interrupts.");
    /* Configure button interrupt. */
    err = gpio_pin_configure_dt(&btn, GPIO_INPUT);
    if (err)
    {
        GLTH_LOGE(TAG, "Error %d: failed to configure %s pin %d", err, btn.port->name, btn.pin);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&btn, GPIO_INT_EDGE_TO_ACTIVE);
    if (err)
    {
        GLTH_LOGE(TAG,
                  "Error %d: failed to configure interrupt on %s pin %d",
                  err,
                  btn.port->name,
                  btn.pin);
        return err;
    }

    GLTH_LOGD(TAG, "Registering button interrupt handlers.");
    /* Register interrupt handler. */
    gpio_init_callback(&btn_cb_data, btn_pressed, BIT(btn.pin));
    gpio_add_callback(btn.port, &btn_cb_data);

    ei_impulse_result_t result = {0};

    GLTH_LOGI(TAG, "Press button to sample data.");

    while (1)
    {
        k_sem_take(&btn_press, K_FOREVER);
        signal_t features_signal;
        features_signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
        features_signal.get_data = &sample_accel;

        GLTH_LOGD(TAG, "Running classifier.");
        EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, true);
        if (res != 0)
        {
            GLTH_LOGE(TAG, "Failed running classifier.");
            return 1;
        }

        uint8_t buf[EI_CLASSIFIER_LABEL_COUNT * 20] = {0};

        ZCBOR_STATE_E(zse, 1, buf, sizeof(buf), EI_CLASSIFIER_LABEL_COUNT);
        bool ok = zcbor_map_start_encode(zse, 1);
        if (!ok)
        {
            GLTH_LOGE(TAG, "Failed to start encoding map.");
            return 1;
        }
        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
        {
            ok = zcbor_tstr_put_term(zse, result.classification[ix].label);
            if (!ok)
            {
                GLTH_LOGE(TAG, "Failed to encode label name.");
                return 1;
            }

            ok = zcbor_float32_put(zse, result.classification[ix].value);
            if (!ok)
            {
                GLTH_LOGE(TAG, "Failed to encode label value.");
                return 1;
            }
            GLTH_LOGI(TAG,
                      "%s: %.5f\n",
                      result.classification[ix].label,
                      result.classification[ix].value);
        }

        ok = zcbor_map_end_encode(zse, 1);
        if (!ok)
        {
            GLTH_LOGE(TAG, "Failed to close map.");
            return 1;
        }
        GLTH_LOGD(TAG, "Uploading classification results.");
        err = golioth_stream_set_sync(client,
                                      "class",
                                      GOLIOTH_CONTENT_TYPE_CBOR,
                                      buf,
                                      (intptr_t) zse->payload - (intptr_t) buf,
                                      5);
        if (err)
        {
            GLTH_LOGE(TAG, "Failed streaming classification results.");
        }

        GLTH_LOGD(TAG, "Uploading accelerometer readings.");
        err = golioth_stream_set_blockwise_sync(client,
                                                "accel",
                                                GOLIOTH_CONTENT_TYPE_OCTET_STREAM,
                                                upload_accel_readings,
                                                NULL);
        if (err)
        {
            GLTH_LOGE(TAG, "Failed streaming accelerometer readings.");
        }
    }
}
