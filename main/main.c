#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUTTON_PIN            GPIO_NUM_16
#define PRESSED_LEVEL         0
#define QUIET_WINDOW_MS       20
#define LINE_CHECK_SAMPLES    1000


static volatile uint32_t edgeCount = 0;
static volatile int64_t  firstEdgeUs = 0;
static volatile int64_t  lastEdgeUs = 0;
static TaskHandle_t      buttonTaskHandle = NULL;
static void IRAM_ATTR handleButtonEdge(void *argument) {
    int64_t nowUs = esp_timer_get_time();

    if (edgeCount == 0) {
        firstEdgeUs = nowUs;
    }
    lastEdgeUs = nowUs;
    edgeCount++;

    BaseType_t isHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(buttonTaskHandle, &isHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(isHigherPriorityTaskWoken);
}

static void printLineRestCheck(gpio_num_t pin) {
    uint32_t highCount = 0;
    for (uint32_t sampleIndex = 0; sampleIndex < LINE_CHECK_SAMPLES; sampleIndex++) {
        highCount += gpio_get_level(pin);
    }
    printf("LINE CHECK: %lu of %d reads are HIGH\n",
           (unsigned long)highCount, LINE_CHECK_SAMPLES);
}

static void buttonEventTask(void *taskParameter) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(QUIET_WINDOW_MS)) != 0) {
        }

        uint32_t transitionCount  = edgeCount - 1;
        int64_t  bounceDurationUs = lastEdgeUs - firstEdgeUs;
        int      finalLevel       = gpio_get_level(BUTTON_PIN);
        edgeCount = 0;

        const char *eventName = finalLevel == PRESSED_LEVEL ? "PRESS" : "RELEASE";
        printf("%s: transitions=%lu, pulses=%lu, bounce=%lld us\n",
               eventName,
               (unsigned long)transitionCount,
               (unsigned long)(transitionCount / 2),
               (long long)bounceDurationUs);
    }
}

void app_main(void) {
    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
    printLineRestCheck(BUTTON_PIN);
    xTaskCreate(buttonEventTask, "BUTTON_EVENT", 4096, NULL, 10, &buttonTaskHandle);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_PIN, handleButtonEdge, NULL));
}
