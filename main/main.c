/*
 * LED blink with FreeRTOS
 */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "ssd1306.h"
#include "gfx.h"

#include "pico/stdlib.h"
#include <stdio.h>

const uint BTN_1_OLED = 28;
const uint BTN_2_OLED = 26;
const uint BTN_3_OLED = 27;

const uint LED_1_OLED = 20;
const uint LED_2_OLED = 21;
const uint LED_3_OLED = 22;

const int PINY = 19; // echo
const int PINX = 18; // trig

SemaphoreHandle_t xSemaphoreTrigger;

QueueHandle_t xQueueTime;
QueueHandle_t xQueueDistance;

void pin_callback(uint gpio, uint32_t events) {
    static int64_t start_time = 0;
    int64_t time;

    if (events == GPIO_IRQ_EDGE_RISE) {  
        start_time = to_us_since_boot(get_absolute_time());
    } else if (events == GPIO_IRQ_EDGE_FALL) {  // Ela vai calcular o tempo que o ECHO ficou em nível alto e enviar isso para a xQueueTime.
        time = to_us_since_boot(get_absolute_time()) - start_time;
        xQueueSendFromISR(xQueueTime, &time, NULL);
    }
}

void trigger_task(void *p) { 
    gpio_init(PINX);
    gpio_set_dir(PINX, GPIO_OUT);
    gpio_put(PINX, 0);

    while (1) {
        gpio_put(PINX, 1);
        sleep_us(10);
        gpio_put(PINX, 0);

        xSemaphoreGive(xSemaphoreTrigger); // sinaliza q o pulso foi enviado 
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void echo_task(void *p) {
    gpio_init(PINY);
    gpio_set_dir(PINY, GPIO_IN);
    gpio_pull_up(PINY);

    gpio_set_irq_enabled_with_callback(PINY, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &pin_callback);
    int64_t time;
    int distance;

    while (1) {
        if (xSemaphoreTake(xSemaphoreTrigger, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (xQueueReceive(xQueueTime, &time, pdMS_TO_TICKS(100))) {
                distance = (int)(time * 0.01715);
                if (distance >= 2 && distance <= 400) {
                    xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
                } else {
                    distance = -1;  
                    xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
                }
            } else {
                distance = -1; 
                xQueueSend(xQueueDistance, &distance, portMAX_DELAY);
            }
        }
    }
    
}

void oled_task(void *p) { 
    int distance;
    char dist_str[32]; 

    printf("Inicializando Driver\n");
    ssd1306_init(); 

    printf("Inicializando GLX\n");
    ssd1306_t disp;
    gfx_init(&disp, 128, 32);

    while (1) {
        if (xQueueReceive(xQueueDistance, &distance, portMAX_DELAY)) {
            gfx_clear_buffer(&disp);
            if (distance == -1) {
                snprintf(dist_str, sizeof(dist_str), "Erro"); 
            } else {
                snprintf(dist_str, sizeof(dist_str), "Dist: %d cm", distance);
            }
            gfx_draw_string(&disp, 0, 0, 1, dist_str); 
            gfx_show(&disp);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}

void oled1_btn_led_init(void) {
    gpio_init(LED_1_OLED);
    gpio_set_dir(LED_1_OLED, GPIO_OUT);

    gpio_init(LED_2_OLED);
    gpio_set_dir(LED_2_OLED, GPIO_OUT);

    gpio_init(LED_3_OLED);
    gpio_set_dir(LED_3_OLED, GPIO_OUT);

    gpio_init(BTN_1_OLED);
    gpio_set_dir(BTN_1_OLED, GPIO_IN);
    gpio_pull_up(BTN_1_OLED);

    gpio_init(BTN_2_OLED);
    gpio_set_dir(BTN_2_OLED, GPIO_IN);
    gpio_pull_up(BTN_2_OLED);

    gpio_init(BTN_3_OLED);
    gpio_set_dir(BTN_3_OLED, GPIO_IN);
    gpio_pull_up(BTN_3_OLED);
}



int main() {
    stdio_init_all();

    xQueueTime = xQueueCreate(32, sizeof(int64_t)); 
    xQueueDistance = xQueueCreate(32, sizeof(int)); 

    xSemaphoreTrigger = xSemaphoreCreateBinary();

    xTaskCreate(trigger_task, "Trigger", 4096, NULL, 1, NULL);
    xTaskCreate(echo_task, "Echo", 4096, NULL, 1, NULL);
    xTaskCreate(oled_task, "OLED", 4096, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true);
}