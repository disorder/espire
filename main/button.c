#include "config.h"
#include "device.h"
#include "button.h"
#include "check.h"
#include "util.h"

#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/task.h"

#include "esp_log.h"
static const char *TAG = "button";

#define ESP_INTR_FLAG_DEFAULT 0
static int installed = 0;

//int button_count = 0;
static list_t buttons = {0};

static task_t *task = NULL;

// interrupt service routine, called when the button is pressed
//void IRAM_ATTR button_isr_handler(struct QueueDefinition **qd)
void IRAM_ATTR button_isr_handler(SemaphoreHandle_t *sem)
{
    xSemaphoreGiveFromISR(sem, NULL);
}

// this is easier for adding more caller-managed data into button_t
void button_add(button_t *b)
{
    list_t *item = list_append(&buttons, (void *) b);
    assert(item != NULL);
    b->last = 0;

    ESP_LOGI(TAG, "Enabling GPIO %d", b->gpio);
    check_gpio(b->gpio, BUTTON);
    if (BUTTON_STATE_ON == 1)
        gpio_set_pull_mode(b->gpio, GPIO_PULLDOWN_ONLY);
    else
        gpio_set_pull_mode(b->gpio, GPIO_PULLUP_ONLY);

    if (b->gpio >= 34) {
        // warn but don't assert, this can be handled externally
        ESP_LOGE(TAG, "button GPIO %d has no internal pullup/pulldown", b->gpio);
    }

    ESP_LOGI(TAG, "button GPIO %d for wakeup: %d", b->gpio, esp_sleep_is_valid_wakeup_gpio(b->gpio));
    esp_sleep_enable_ext0_wakeup(b->gpio, BUTTON_STATE_ON);

    // create the binary semaphore
    b->sem = xSemaphoreCreateBinary();

    esp_rom_gpio_pad_select_gpio(b->gpio);

    // set the correct direction
    gpio_set_direction(b->gpio, GPIO_MODE_INPUT);

    // enable interrupt on falling (1->0) edge for button pin
    //gpio_set_intr_type(b->gpio, GPIO_INTR_POSEDGE);
    gpio_set_intr_type(b->gpio, GPIO_INTR_ANYEDGE);

    // install ISR service with default configuration
    if (!installed) {
        gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
        installed = 1;
    }

    // attach the interrupt service routine
    gpio_isr_handler_add(b->gpio, (void (*)(void *)) button_isr_handler, b->sem);

    //button_count += 1;
}

void button_init(int *gpios, int count, void (*handler)(button_t *b))
{
    ESP_LOGI(TAG, "init: %d buttons", count);

    if (count == 0)
        return;

    // configure button and led pins as GPIO pins
    for (int i=0; i<count; i++) {
        button_t *b = calloc(1, sizeof(button_t));
        list_t *item = list_append(&buttons, (void *) b);
        assert(item != NULL);

        b->gpio = gpios[i];
        b->handler = handler;
        b->state = BUTTON_STATE_OFF;
        button_add(b);
    }
}

static void button_task()
{
    while (1) {
        // wait for the notification from the ISR
        for (list_t *item = buttons.next; item != NULL; item = item->next) {
            button_t *b = (button_t *) item->data;
            TickType_t now = xTaskGetTickCount();
            int sem = xSemaphoreTake(b->sem, 0);
            if(sem == pdTRUE ||
               (b->state == BUTTON_STATE_ON && TICK_TO_MS(now - b->change) > BUTTON_REPEAT_MS)) {
                if (now > b->last + MS_TO_TICK(BUTTON_DEBOUNCE_MS)) {
                    b->last = now;
                    if (sem == pdTRUE)
                        b->change = now;
                    int length = TICK_TO_MS(now - b->change);
                    int state = gpio_get_level(b->gpio);
                    if (state == b->state) {
                        b->changed = 0;
                    } else {
                        b->state = state;
                        b->changed = 1;
                        b->change = now;
                    }

                    int longs = (b->state == BUTTON_STATE_ON)? length / BUTTON_LONG_MS : 0;
                    if (longs > b->longs) {
                        b->longpress = 1;
                        b->longs += 1;
                        //b->longs = longs;
                    } else {
                        b->longpress = 0;
                    }

                    int reps = (b->state == BUTTON_STATE_ON)? length / BUTTON_REPEAT_MS : 0;
                    if (reps > b->reps) {
                        b->repeated = 1;
                        b->reps += 1;
                        //b->reps = reps;
                    } else {
                        b->repeated = 0;
                    }

                    if (b->changed || b->repeated || b->longpress) {
                        ESP_LOGI(TAG, "triggered GPIO %d: %d changed=%d reps(%d)=%d longs(%d)=%d", b->gpio, state, b->changed, b->repeated, b->reps, b->longpress, b->longs);
                        if (LIST(button_t, item, handler) != NULL && !esp.sleeping)
                            LIST(button_t, item, handler)((button_t *) item->data);
                    }

                    // clear after call so handler sees final counts
                    if (b->changed && state != BUTTON_STATE_ON) {
                        b->longpress = 0;
                        b->repeated = 0;
                        b->reps = 0;
                        b->longs = 0;
                    }
                    break;
                }
            }
        }
        // fraction of repeat time to allow for somewhat precise response
        _vTaskDelay(MS_TO_TICK(BUTTON_REPEAT_MS / 10));
    }
}

void button_run()
{
    if (buttons.next == NULL) {
        ESP_LOGI(TAG, "no buttons");
        return;
    }
    // start the task that will handle the button
    xxTaskCreate((void (*)(void *)) button_task, "button_task", 3*1024, NULL, 5, &task);
}
