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
#include "bsp/esp-bsp.h"

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
        stepper_set_rpm(s_stepper, 10);
        stepper_release(s_stepper);
    }
    return err;
}

static esp_err_t app_driver_window_covering_set_position(esp_matter_attr_val_t *val)
{
    /* Position is in percent100ths: 0 = fully open, 10000 = fully closed */
    ESP_LOGI(TAG, "Window covering target position: %d (%.1f%%)", val->val.u16, (float)val->val.u16 / 100.0);

    /* For now, instantly report that we've reached the target position.
     * In Phase C, this is where we'll drive the stepper motor. */
    esp_matter_attr_val_t current_val = esp_matter_nullable_uint16(val->val.u16);
    attribute::update(window_covering_endpoint_id, WindowCovering::Id,
        WindowCovering::Attributes::CurrentPositionLiftPercent100ths::Id, &current_val);

    /* Also update the non-100ths percentage attribute */
    esp_matter_attr_val_t pct_val = esp_matter_nullable_uint8((uint8_t)(val->val.u16 / 100));
    attribute::update(window_covering_endpoint_id, WindowCovering::Id,
        WindowCovering::Attributes::CurrentPositionLiftPercentage::Id, &pct_val);

    /* Set operational status to stopped (0) since movement is instant */
    esp_matter_attr_val_t status_val = esp_matter_uint8(0);
    attribute::update(window_covering_endpoint_id, WindowCovering::Id,
        WindowCovering::Attributes::OperationalStatus::Id, &status_val);

    return ESP_OK;
}

static void app_driver_button_toggle_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    /* In Phase C, this could toggle between fully open and fully closed */
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
    ESP_ERROR_CHECK(iot_button_register_cb(btns[0], BUTTON_PRESS_DOWN, app_driver_button_toggle_cb, NULL));

    return (app_driver_handle_t)btns[0];
}
