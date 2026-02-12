#include "stepper_driver.h"

#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

static const char *TAG = "stepper";

/* 28BYJ-48 gear ratio: 63.68395:1
 * Internal motor steps per revolution: 32 (full-step), 64 (half-step)
 * Output shaft steps per revolution: ~2038 (full-step), ~4076 (half-step) */
#define STEPS_PER_REV_FULL  2038
#define STEPS_PER_REV_HALF  4076

/* Half-step sequence (8 phases) - smoother motion, higher resolution */
static const uint8_t half_step_seq[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

/* Full-step sequence (4 phases) - more torque */
static const uint8_t full_step_seq[4][4] = {
    {1, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 1},
    {1, 0, 0, 1},
};

struct stepper_driver {
    gpio_num_t pins[4];
    stepper_mode_t mode;
    int32_t position;
    uint32_t step_delay_us;  /* microseconds between steps */
    int8_t phase;            /* current phase index */
};

static void set_pins(stepper_handle_t handle, const uint8_t *levels)
{
    for (int i = 0; i < 4; i++) {
        gpio_set_level(handle->pins[i], levels[i]);
    }
}

static void step_once(stepper_handle_t handle, int direction)
{
    int num_phases = (handle->mode == STEPPER_MODE_HALF_STEP) ? 8 : 4;
    const uint8_t (*seq)[4] = (handle->mode == STEPPER_MODE_HALF_STEP)
                                  ? half_step_seq
                                  : full_step_seq;

    handle->phase += direction;
    if (handle->phase >= num_phases) {
        handle->phase = 0;
    } else if (handle->phase < 0) {
        handle->phase = num_phases - 1;
    }

    set_pins(handle, seq[handle->phase]);
}

esp_err_t stepper_init(const stepper_config_t *config, stepper_handle_t *handle)
{
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }

    stepper_handle_t drv = calloc(1, sizeof(struct stepper_driver));
    if (!drv) {
        return ESP_ERR_NO_MEM;
    }

    drv->pins[0] = config->pin_in1;
    drv->pins[1] = config->pin_in2;
    drv->pins[2] = config->pin_in3;
    drv->pins[3] = config->pin_in4;
    drv->mode = config->mode;
    drv->position = 0;
    drv->phase = 0;

    /* Default 10 RPM */
    uint32_t steps_per_rev = (drv->mode == STEPPER_MODE_HALF_STEP)
                                 ? STEPS_PER_REV_HALF
                                 : STEPS_PER_REV_FULL;
    drv->step_delay_us = (uint32_t)(60.0 * 1000000.0 / (10.0 * steps_per_rev));

    /* Configure GPIO pins as outputs */
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    for (int i = 0; i < 4; i++) {
        io_conf.pin_bit_mask = (1ULL << drv->pins[i]);
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure GPIO %d: %s",
                     drv->pins[i], esp_err_to_name(err));
            free(drv);
            return err;
        }
        gpio_set_level(drv->pins[i], 0);
    }

    ESP_LOGI(TAG, "Initialized: pins=[%d,%d,%d,%d] mode=%s",
             drv->pins[0], drv->pins[1], drv->pins[2], drv->pins[3],
             drv->mode == STEPPER_MODE_HALF_STEP ? "half-step" : "full-step");

    *handle = drv;
    return ESP_OK;
}

esp_err_t stepper_move_steps(stepper_handle_t handle, int32_t steps)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (steps == 0) {
        return ESP_OK;
    }

    int direction = (steps > 0) ? 1 : -1;
    int32_t remaining = abs(steps);

    ESP_LOGI(TAG, "Moving %ld steps (%s)", (long)steps,
             direction > 0 ? "CW" : "CCW");

    /* Yield every YIELD_STEPS to let other FreeRTOS tasks run.
     * Critical on single-core ESP32-C6 where esp_rom_delay_us busy-waits. */
    #define YIELD_STEPS 20

    int32_t step_count = 0;
    while (remaining > 0) {
        step_once(handle, direction);
        handle->position += direction;
        remaining--;
        step_count++;
        esp_rom_delay_us(handle->step_delay_us);

        if (step_count >= YIELD_STEPS) {
            step_count = 0;
            vTaskDelay(1);
        }
    }

    return ESP_OK;
}

esp_err_t stepper_set_rpm(stepper_handle_t handle, float rpm)
{
    if (!handle || rpm <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t steps_per_rev = (handle->mode == STEPPER_MODE_HALF_STEP)
                                 ? STEPS_PER_REV_HALF
                                 : STEPS_PER_REV_FULL;
    handle->step_delay_us = (uint32_t)(60.0 * 1000000.0 / (rpm * steps_per_rev));

    ESP_LOGI(TAG, "Speed set to %.1f RPM (delay=%lu us)", rpm,
             (unsigned long)handle->step_delay_us);

    return ESP_OK;
}

esp_err_t stepper_release(stepper_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < 4; i++) {
        gpio_set_level(handle->pins[i], 0);
    }

    ESP_LOGI(TAG, "Coils released");
    return ESP_OK;
}

int32_t stepper_get_position(stepper_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    return handle->position;
}

esp_err_t stepper_reset_position(stepper_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->position = 0;
    return ESP_OK;
}

esp_err_t stepper_set_position(stepper_handle_t handle, int32_t position)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->position = position;
    return ESP_OK;
}

esp_err_t stepper_deinit(stepper_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Release coils before cleanup */
    stepper_release(handle);

    for (int i = 0; i < 4; i++) {
        gpio_reset_pin(handle->pins[i]);
    }

    free(handle);
    ESP_LOGI(TAG, "Driver deinitialized");
    return ESP_OK;
}
