/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <platform/PlatformManager.h>
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <nvs.h>

#include <stepper_driver.h>
#include <app_priv.h>

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t window_covering_endpoint_id;

/* Stepper motor: Seeed XIAO ESP32-C6 pins D0-D3 */
#define STEPPER_IN1 GPIO_NUM_0
#define STEPPER_IN2 GPIO_NUM_1
#define STEPPER_IN3 GPIO_NUM_2
#define STEPPER_IN4 GPIO_NUM_21

static stepper_handle_t s_stepper = NULL;

/* Default full travel (uncalibrated) = 1 revolution of 28BYJ-48 output shaft */
#define STEPPER_DEFAULT_TRAVEL_STEPS 2048

/* FreeRTOS task and queue for non-blocking stepper control */
static QueueHandle_t s_stepper_queue = NULL;
static TaskHandle_t s_stepper_task = NULL;

/* --- Calibration --- */

#define NVS_NAMESPACE    "blinds_cal"
#define NVS_KEY_OPEN     "open_steps"
#define NVS_KEY_CLOSED   "closed_steps"
#define NVS_KEY_POSITION "position"

#define CAL_JOG_RPM      5
#define CAL_JOG_CHUNK    20   /* steps per jog iteration (response time ~120ms at 5 RPM) */
#define NORMAL_RPM       10

typedef enum {
    CAL_STATE_NONE,     /* Normal operation */
    CAL_STATE_READY,    /* Calibration entered, closed marked, waiting for jog/save */
    CAL_STATE_JOGGING,  /* Motor jogging during calibration */
} cal_state_t;

static cal_state_t s_cal_state = CAL_STATE_NONE;
static int8_t s_jog_direction = 1;  /* +1 = CW (default), -1 = CCW */

/* Calibration endpoints (in stepper steps) */
static int32_t s_cal_open_steps = 0;
static int32_t s_cal_closed_steps = STEPPER_DEFAULT_TRAVEL_STEPS;
static bool s_calibrated = false;

/* --- Stepper command types --- */

typedef enum {
    STEPPER_CMD_POSITION,   /* Move to a percent100ths position */
    STEPPER_CMD_JOG,        /* Jog continuously in a direction */
    STEPPER_CMD_STOP,       /* Stop jogging */
} stepper_cmd_type_t;

typedef struct {
    stepper_cmd_type_t type;
    int32_t value;  /* percent100ths for POSITION, direction (+1/-1) for JOG */
} stepper_cmd_t;

/* --- NVS helpers --- */

static esp_err_t cal_save_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_i32(handle, NVS_KEY_OPEN, s_cal_open_steps);
    nvs_set_i32(handle, NVS_KEY_CLOSED, s_cal_closed_steps);
    err = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Calibration saved to NVS: open=%d, closed=%d",
             (int)s_cal_open_steps, (int)s_cal_closed_steps);
    return err;
}

static esp_err_t cal_save_position(int32_t position)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    nvs_set_i32(handle, NVS_KEY_POSITION, position);
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t cal_load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No calibration data in NVS, using defaults");
        return err;
    }

    int32_t open_steps, closed_steps;
    err = nvs_get_i32(handle, NVS_KEY_OPEN, &open_steps);
    if (err == ESP_OK) {
        err = nvs_get_i32(handle, NVS_KEY_CLOSED, &closed_steps);
    }

    if (err == ESP_OK) {
        s_cal_open_steps = open_steps;
        s_cal_closed_steps = closed_steps;
        s_calibrated = true;
        ESP_LOGI(TAG, "Calibration loaded: open=%d, closed=%d, travel=%d steps",
                 (int)s_cal_open_steps, (int)s_cal_closed_steps,
                 (int)(s_cal_closed_steps - s_cal_open_steps));
    }

    /* Restore last known stepper position */
    int32_t position = 0;
    if (nvs_get_i32(handle, NVS_KEY_POSITION, &position) == ESP_OK) {
        stepper_set_position(s_stepper, position);
        ESP_LOGI(TAG, "Restored stepper position: %d", (int)position);
    }

    nvs_close(handle);
    return err;
}

/* --- Stepper task --- */

static void stepper_task(void *arg)
{
    stepper_cmd_t cmd;
    while (true) {
        if (xQueueReceive(s_stepper_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            switch (cmd.type) {

            case STEPPER_CMD_POSITION: {
                int32_t travel = s_cal_closed_steps - s_cal_open_steps;
                int32_t target_steps = s_cal_open_steps +
                    (int32_t)((int64_t)cmd.value * travel / 10000);
                int32_t current_steps = stepper_get_position(s_stepper);
                int32_t delta = target_steps - current_steps;

                if (delta != 0) {
                    ESP_LOGI(TAG, "Moving stepper: %d -> %d (%d steps)",
                             (int)current_steps, (int)target_steps, (int)delta);
                    stepper_move_steps(s_stepper, delta);
                    stepper_release(s_stepper);
                }

                /* Save position to NVS for restore on reboot */
                cal_save_position(stepper_get_position(s_stepper));

                /* Report position to CHIP stack (lock for thread safety) */
                uint16_t percent100ths = (uint16_t)cmd.value;
                chip::DeviceLayer::PlatformMgr().LockChipStack();

                esp_matter_attr_val_t current_val = esp_matter_nullable_uint16(percent100ths);
                attribute::update(window_covering_endpoint_id, WindowCovering::Id,
                    WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &current_val);

                esp_matter_attr_val_t pct_val = esp_matter_nullable_uint8((uint8_t)(percent100ths / 100));
                attribute::update(window_covering_endpoint_id, WindowCovering::Id,
                    WindowCovering::Attributes::CurrentPositionLiftPercentage::Id, &pct_val);

                esp_matter_attr_val_t status_val = esp_matter_uint8(0);
                attribute::update(window_covering_endpoint_id, WindowCovering::Id,
                    WindowCovering::Attributes::OperationalStatus::Id, &status_val);

                chip::DeviceLayer::PlatformMgr().UnlockChipStack();
                break;
            }

            case STEPPER_CMD_JOG: {
                int8_t direction = (int8_t)cmd.value;
                stepper_set_rpm(s_stepper, CAL_JOG_RPM);
                ESP_LOGI(TAG, "Jogging %s at %d RPM", direction > 0 ? "CW" : "CCW", CAL_JOG_RPM);

                while (true) {
                    stepper_move_steps(s_stepper, direction * CAL_JOG_CHUNK);

                    /* Check for stop or direction change between chunks */
                    stepper_cmd_t next;
                    if (xQueueReceive(s_stepper_queue, &next, 0) == pdTRUE) {
                        if (next.type == STEPPER_CMD_STOP) {
                            break;
                        }
                        if (next.type == STEPPER_CMD_JOG) {
                            direction = (int8_t)next.value;
                            ESP_LOGI(TAG, "Jog direction changed: %s", direction > 0 ? "CW" : "CCW");
                        }
                    }
                }

                stepper_release(s_stepper);
                stepper_set_rpm(s_stepper, NORMAL_RPM);
                ESP_LOGI(TAG, "Jog stopped at position: %d", (int)stepper_get_position(s_stepper));
                break;
            }

            case STEPPER_CMD_STOP:
                /* Should only arrive during JOG, but handle gracefully */
                stepper_release(s_stepper);
                break;
            }
        }
    }
}

/* --- Calibration button callbacks --- */

static void cal_button_long_press_cb(void *arg, void *data)
{
    if (s_cal_state == CAL_STATE_NONE) {
        /* Enter calibration mode — mark current position as "closed" */
        s_cal_state = CAL_STATE_READY;
        s_jog_direction = 1;  /* Default CW */
        s_cal_closed_steps = stepper_get_position(s_stepper);
        ESP_LOGI(TAG, "CALIBRATION: Entered. Closed position = step %d",
                 (int)s_cal_closed_steps);
        ESP_LOGI(TAG, "CALIBRATION: Hold button to jog CW. Double-click to reverse. Single-click to save.");
    } else if (s_cal_state == CAL_STATE_READY) {
        /* Start jogging */
        s_cal_state = CAL_STATE_JOGGING;
        stepper_cmd_t cmd = { .type = STEPPER_CMD_JOG, .value = s_jog_direction };
        xQueueOverwrite(s_stepper_queue, &cmd);
    }
}

static void cal_button_long_press_up_cb(void *arg, void *data)
{
    if (s_cal_state == CAL_STATE_JOGGING) {
        /* Stop jogging */
        s_cal_state = CAL_STATE_READY;
        stepper_cmd_t cmd = { .type = STEPPER_CMD_STOP, .value = 0 };
        xQueueOverwrite(s_stepper_queue, &cmd);
    }
}

static void cal_button_double_click_cb(void *arg, void *data)
{
    if (s_cal_state == CAL_STATE_READY) {
        s_jog_direction = -s_jog_direction;
        ESP_LOGI(TAG, "CALIBRATION: Direction reversed to %s",
                 s_jog_direction > 0 ? "CW" : "CCW");
    }
}

static void cal_button_single_click_cb(void *arg, void *data)
{
    if (s_cal_state == CAL_STATE_READY) {
        /* Save open position and exit calibration */
        s_cal_open_steps = stepper_get_position(s_stepper);
        s_calibrated = true;
        s_cal_state = CAL_STATE_NONE;

        int32_t travel = s_cal_closed_steps - s_cal_open_steps;
        ESP_LOGI(TAG, "CALIBRATION: Complete. Open=%d, Closed=%d, Travel=%d steps",
                 (int)s_cal_open_steps, (int)s_cal_closed_steps, (int)travel);

        cal_save_to_nvs();
        cal_save_position(s_cal_open_steps);

        /* Update Matter attributes — we're at the open position (0%) */
        chip::DeviceLayer::PlatformMgr().LockChipStack();

        esp_matter_attr_val_t pos_val = esp_matter_nullable_uint16(0);
        attribute::update(window_covering_endpoint_id, WindowCovering::Id,
            WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &pos_val);
        attribute::update(window_covering_endpoint_id, WindowCovering::Id,
            WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id, &pos_val);

        esp_matter_attr_val_t pct_val = esp_matter_nullable_uint8(0);
        attribute::update(window_covering_endpoint_id, WindowCovering::Id,
            WindowCovering::Attributes::CurrentPositionLiftPercentage::Id, &pct_val);

        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
    }
}

/* --- Public API --- */

esp_err_t app_driver_stepper_init(void)
{
    stepper_config_t config = {
        .pin_in1 = STEPPER_IN1,
        .pin_in2 = STEPPER_IN2,
        .pin_in3 = STEPPER_IN3,
        .pin_in4 = STEPPER_IN4,
        .mode = STEPPER_MODE_FULL_STEP,
    };
    esp_err_t err = stepper_init(&config, &s_stepper);
    if (err == ESP_OK) {
        stepper_set_rpm(s_stepper, NORMAL_RPM);
        stepper_release(s_stepper);
    }

    /* Load calibration data and last known position from NVS */
    cal_load_from_nvs();

    s_stepper_queue = xQueueCreate(1, sizeof(stepper_cmd_t));
    if (!s_stepper_queue) {
        ESP_LOGE(TAG, "Failed to create stepper queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t ret = xTaskCreate(stepper_task, "stepper", 4096, NULL, 5, &s_stepper_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create stepper task");
        return ESP_FAIL;
    }

    return err;
}

uint16_t app_driver_get_initial_position(void)
{
    if (!s_calibrated) return 0;

    int32_t travel = s_cal_closed_steps - s_cal_open_steps;
    if (travel == 0) return 0;

    int32_t pos = stepper_get_position(s_stepper);
    int32_t percent100ths = (int32_t)((int64_t)(pos - s_cal_open_steps) * 10000 / travel);

    if (percent100ths < 0) percent100ths = 0;
    if (percent100ths > 10000) percent100ths = 10000;

    return (uint16_t)percent100ths;
}

static esp_err_t app_driver_window_covering_set_position(esp_matter_attr_val_t *val)
{
    if (s_cal_state != CAL_STATE_NONE) {
        ESP_LOGW(TAG, "Ignoring position command during calibration");
        return ESP_OK;
    }

    uint16_t target_percent100ths = val->val.u16;
    ESP_LOGI(TAG, "Window covering target position: %d (%.1f%%)",
             target_percent100ths, (float)target_percent100ths / 100.0);

    stepper_cmd_t cmd = { .type = STEPPER_CMD_POSITION, .value = target_percent100ths };
    xQueueOverwrite(s_stepper_queue, &cmd);

    return ESP_OK;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    if (endpoint_id == window_covering_endpoint_id) {
        if (cluster_id == WindowCovering::Id) {
            if (attribute_id == WindowCovering::Attributes::TargetPositionLiftPercent100ths::Id) {
                err = app_driver_window_covering_set_position(val);
            }
        }
    }
    return err;
}

app_driver_handle_t app_driver_button_init()
{
    /* Initialize button */
    button_handle_t btns[BSP_BUTTON_NUM];
    ESP_ERROR_CHECK(bsp_iot_button_create(btns, NULL, BSP_BUTTON_NUM));

    /* Register calibration button callbacks */
    ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_LONG_PRESS_START, cal_button_long_press_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_LONG_PRESS_UP, cal_button_long_press_up_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_DOUBLE_CLICK, cal_button_double_click_cb, NULL));
    ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_SINGLE_CLICK, cal_button_single_click_cb, NULL));

    return (app_driver_handle_t)btns[0];
}
