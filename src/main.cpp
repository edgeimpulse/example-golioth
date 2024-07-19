/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ei_glth, LOG_LEVEL_DBG);

#include <golioth/client.h>
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

const struct device *accel = DEVICE_DT_GET_ONE(adi_adxl362);

static float readings[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

int sample_accel(size_t offset, size_t length, float *out_ptr)
{
    if (length > EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE)
    {
        GLTH_LOGE(TAG, "Unexpected features length: %d", length);
        return 1;
    }
    size_t i = 0;
    while (i < length)
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
    memcpy(out_ptr, readings, length * sizeof(float));
    return 0;
}

int main()
{
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

    ei_impulse_result_t result = {0};

    while (1)
    {
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

        for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
        {
            GLTH_LOGI(TAG,
                      "%s: %.5f\n",
                      result.classification[ix].label,
                      result.classification[ix].value);
        }
        k_sleep(K_SECONDS(10));
    }
}
