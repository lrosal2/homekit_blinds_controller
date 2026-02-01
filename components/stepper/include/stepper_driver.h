#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STEPPER_MODE_FULL_STEP = 0,
    STEPPER_MODE_HALF_STEP = 1,
} stepper_mode_t;

typedef struct {
    gpio_num_t pin_in1;
    gpio_num_t pin_in2;
    gpio_num_t pin_in3;
    gpio_num_t pin_in4;
    stepper_mode_t mode;
} stepper_config_t;

typedef struct stepper_driver *stepper_handle_t;

/**
 * @brief Initialize stepper motor driver
 *
 * @param config GPIO pin and mode configuration
 * @param handle Output handle for subsequent calls
 * @return ESP_OK on success
 */
esp_err_t stepper_init(const stepper_config_t *config, stepper_handle_t *handle);

/**
 * @brief Move the motor a given number of steps
 *
 * Positive steps = clockwise (open), negative = counter-clockwise (close).
 * Blocks until movement is complete.
 *
 * @param handle Stepper handle
 * @param steps Number of steps (positive = CW, negative = CCW)
 * @return ESP_OK on success
 */
esp_err_t stepper_move_steps(stepper_handle_t handle, int32_t steps);

/**
 * @brief Set motor speed in RPM (output shaft)
 *
 * @param handle Stepper handle
 * @param rpm Speed in revolutions per minute (1-15 recommended for 28BYJ-48)
 * @return ESP_OK on success
 */
esp_err_t stepper_set_rpm(stepper_handle_t handle, float rpm);

/**
 * @brief De-energize all coils to save power
 *
 * The 28BYJ-48 draws holding current when stopped. Call this after movement
 * is complete to cut power. The motor will not hold position after release.
 *
 * @param handle Stepper handle
 * @return ESP_OK on success
 */
esp_err_t stepper_release(stepper_handle_t handle);

/**
 * @brief Get current step position
 *
 * Position is tracked relative to where the driver was initialized (0).
 *
 * @param handle Stepper handle
 * @return Current position in steps
 */
int32_t stepper_get_position(stepper_handle_t handle);

/**
 * @brief Reset position counter to zero
 *
 * @param handle Stepper handle
 * @return ESP_OK on success
 */
esp_err_t stepper_reset_position(stepper_handle_t handle);

/**
 * @brief Free driver resources
 *
 * @param handle Stepper handle
 * @return ESP_OK on success
 */
esp_err_t stepper_deinit(stepper_handle_t handle);

#ifdef __cplusplus
}
#endif
