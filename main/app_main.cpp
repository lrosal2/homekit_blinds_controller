#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "stepper_driver.h"

static const char *TAG = "main";

/* GPIO pin assignments for ULN2003 driver board
 * D0-IN1, D1-IN2, D2-IN3, D3-IN4 */
#define STEPPER_PIN_IN1  GPIO_NUM_0
#define STEPPER_PIN_IN2  GPIO_NUM_1
#define STEPPER_PIN_IN3  GPIO_NUM_2
#define STEPPER_PIN_IN4  GPIO_NUM_3

/* 28BYJ-48 half-step: 4076 steps = one full revolution of output shaft */
#define STEPS_ONE_REV    4076

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Blinds Controller - Stage 1: Motor Test ===");

    stepper_config_t config = {
        .pin_in1 = STEPPER_PIN_IN1,
        .pin_in2 = STEPPER_PIN_IN2,
        .pin_in3 = STEPPER_PIN_IN3,
        .pin_in4 = STEPPER_PIN_IN4,
        .mode = STEPPER_MODE_HALF_STEP,
    };

    stepper_handle_t motor = NULL;
    esp_err_t err = stepper_init(&config, &motor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize stepper: %s", esp_err_to_name(err));
        return;
    }

    stepper_set_rpm(motor, 10);

    /* Test: rotate forward half a revolution, then back */
    int32_t test_steps = STEPS_ONE_REV / 2;

    ESP_LOGI(TAG, "Moving forward %ld steps...", (long)test_steps);
    stepper_move_steps(motor, test_steps);
    stepper_release(motor);
    ESP_LOGI(TAG, "Position: %ld -- coils released, measure current now (expect ~0A)",
             (long)stepper_get_position(motor));

    ESP_LOGI(TAG, "Pausing 5 seconds -- check motor current draw...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Moving backward %ld steps...", (long)test_steps);
    stepper_move_steps(motor, -test_steps);
    stepper_release(motor);
    ESP_LOGI(TAG, "Position: %ld -- coils released, measure current now (expect ~0A)",
             (long)stepper_get_position(motor));

    ESP_LOGI(TAG, "Motor test complete. Coils are released -- motor current should be ~0A.");
}
