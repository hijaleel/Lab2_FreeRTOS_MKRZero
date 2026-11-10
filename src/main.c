/**
 * ============================================================================
 * Lab 2: FreeRTOS on ATSAMD21G18A (Arduino MKR Zero / ARM Cortex-M0+)
 * Using pure CMSIS ("sam.h") without Arduino.h
 *
 * Individual Part - Task Performance & Scheduler Configuration Exploration:
 *   SCHEDULER_CASE 0: Same Priority (1, 1, 1) - Run to Completion (Preemption=0)
 *   SCHEDULER_CASE 1: Case (a) Task1=3, Task2=2, Task3=1 - Run to Completion (Preemption=0)
 *   SCHEDULER_CASE 2: Case (b) Task1=3, Task2=2, Task3=1 - No Priority Inheritance (Binary Sem)
 *   SCHEDULER_CASE 3: Case (c) Task1=3, Task2=2, Task3=1 - Priority Inheritance Enabled (Mutex)
 *   SCHEDULER_CASE 4: Case (b-inv) Priority Inversion Demo (LedOff=3, Serial=2, LedOn=1, Binary Sem)
 *   SCHEDULER_CASE 5: Case (c-inh) Priority Inheritance Demo (LedOff=3, Serial=2, LedOn=1, Mutex)
 * ============================================================================
 */

#include "sam.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* FreeRTOS Kernel Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* -------------------------------------------------------------------------- */
/* Select Active Test Case (0..5)                                             */
/* Can be overridden via platformio.ini build_flags (-DSCHEDULER_CASE=...)    */
/* -------------------------------------------------------------------------- */
#ifndef SCHEDULER_CASE
#define SCHEDULER_CASE 0
#endif

/* -------------------------------------------------------------------------- */
/* Hardware Pin Definitions (ATSAMD21G18A on Arduino MKR Zero)                */
/* - Digital Pin 1 on MKR Zero header maps to PORTA Pin 23 (PA23)             */
/* - Built-in onboard LED (labeled "L") maps to PORTB Pin 8 (PB08)            */
/* -------------------------------------------------------------------------- */
#define PIN1_MASK           PORT_PA23        /* Header Pin 1 = PA23 (1UL << 23) */
#define ONBOARD_LED_MASK    (1UL << 8)       /* Built-in LED = PB08 */

/* -------------------------------------------------------------------------- */
/* Task Priorities (Each task has its own priority determined by a const int) */
/* -------------------------------------------------------------------------- */
#if (SCHEDULER_CASE == 0)
    /* Baseline: All three tasks have the SAME priority level (Run to Completion) */
    const int PRIORITY_TASK_LED_ON  = 1;
    const int PRIORITY_TASK_LED_OFF = 1;
    const int PRIORITY_TASK_SERIAL  = 1;
    #define CASE_NAME "CASE 0: Same Priority (1,1,1) - Run to Completion (configUSE_PREEMPTION=0)"
#elif (SCHEDULER_CASE == 1)
    /* Case (a): Task 1 Highest (3), Task 2 (2), Task 3 (1) - Run to Completion */
    const int PRIORITY_TASK_LED_ON  = 3;
    const int PRIORITY_TASK_LED_OFF = 2;
    const int PRIORITY_TASK_SERIAL  = 1;
    #define CASE_NAME "CASE (a): Task1=3, Task2=2, Task3=1 - Run to Completion (configUSE_PREEMPTION=0)"
#elif (SCHEDULER_CASE == 2)
    /* Case (b): Task 1 Highest (3), Task 2 (2), Task 3 (1) - No Priority Inheritance */
    const int PRIORITY_TASK_LED_ON  = 3;
    const int PRIORITY_TASK_LED_OFF = 2;
    const int PRIORITY_TASK_SERIAL  = 1;
    #define CASE_NAME "CASE (b): Task1=3, Task2=2, Task3=1 - No Priority Inheritance (Binary Semaphore)"
#elif (SCHEDULER_CASE == 3)
    /* Case (c): Task 1 Highest (3), Task 2 (2), Task 3 (1) - Priority Inheritance Enabled */
    const int PRIORITY_TASK_LED_ON  = 3;
    const int PRIORITY_TASK_LED_OFF = 2;
    const int PRIORITY_TASK_SERIAL  = 1;
    #define CASE_NAME "CASE (c): Task1=3, Task2=2, Task3=1 - Priority Inheritance Enabled (Mutex)"
#elif (SCHEDULER_CASE == 4)
    /* Case (b-inv): Blocking Task Highest (LedOff=3, Serial=2, LedOn=1) - No Priority Inheritance */
    const int PRIORITY_TASK_LED_ON  = 1;
    const int PRIORITY_TASK_LED_OFF = 3;
    const int PRIORITY_TASK_SERIAL  = 2;
    #define CASE_NAME "CASE (b-inversion): High=LedOff(3), Med=Serial(2), Low=LedOn(1) - No Priority Inheritance"
#else
    /* Case (c-inh): Blocking Task Highest (LedOff=3, Serial=2, LedOn=1) - Priority Inheritance Enabled */
    const int PRIORITY_TASK_LED_ON  = 1;
    const int PRIORITY_TASK_LED_OFF = 3;
    const int PRIORITY_TASK_SERIAL  = 2;
    #define CASE_NAME "CASE (c-inheritance): High=LedOff(3), Med=Serial(2), Low=LedOn(1) - Priority Inheritance Enabled"
#endif

/* -------------------------------------------------------------------------- */
/* Task Stack Sizes (in words)                                                */
/* -------------------------------------------------------------------------- */
#define STACK_SIZE_LED_TASKS    ( ( configSTACK_DEPTH_TYPE ) 256 )
#define STACK_SIZE_SERIAL       ( ( configSTACK_DEPTH_TYPE ) 256 )

/* -------------------------------------------------------------------------- */
/* FreeRTOS Synchronization Handles & Telemetry Counters                      */
/* -------------------------------------------------------------------------- */
SemaphoreHandle_t xLedMutex = NULL;

TaskHandle_t xHandleTaskLedOn   = NULL;
TaskHandle_t xHandleTaskLedOff  = NULL;
TaskHandle_t xHandleTaskSerial  = NULL;

volatile uint32_t ulTask1Count = 0;
volatile uint32_t ulTask2Count = 0;
volatile uint32_t ulTask3Count = 0;

/* -------------------------------------------------------------------------- */
/* USB CDC Symbol Bridge (allows pure C sam.h code to also log to USB COM)    */
/* -------------------------------------------------------------------------- */
extern uint8_t SerialUSB[];
extern size_t usb_cdc_write(void *this_ptr, const uint8_t *buf, size_t len) __asm__("_ZN7Serial_5writeEPKhj");
extern bool usb_cdc_dtr(void *this_ptr) __asm__("_ZN7Serial_3dtrEv");

/* -------------------------------------------------------------------------- */
/* Low-Level GPIO & UART Helper Functions (Pure CMSIS / sam.h)                */
/* -------------------------------------------------------------------------- */

static void gpio_init(void) {
    /* Configure Pin 1 (PA23, PORTA) as output */
    PORT->Group[0].DIRSET.reg = PIN1_MASK;
    PORT->Group[0].OUTCLR.reg = PIN1_MASK;

    /* Configure Onboard LED (PB08, PORTB) as output */
    PORT->Group[1].DIRSET.reg = ONBOARD_LED_MASK;
    PORT->Group[1].OUTCLR.reg = ONBOARD_LED_MASK;
}

static inline void gpio_led_on(void) {
    PORT->Group[0].OUTSET.reg = PIN1_MASK;
    PORT->Group[1].OUTSET.reg = ONBOARD_LED_MASK;
}

static inline void gpio_led_off(void) {
    PORT->Group[0].OUTCLR.reg = PIN1_MASK;
    PORT->Group[1].OUTCLR.reg = ONBOARD_LED_MASK;
}

static inline bool gpio_led_is_on(void) {
    return (PORT->Group[0].OUT.reg & PIN1_MASK) != 0;
}

/**
 * @brief Initialize SERCOM5 for Serial UART output on PB22 (TX) / PB23 (RX).
 */
static void uart_init(uint32_t baudrate) {
    uint32_t timeout;

    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE) |
                        GCLK_CLKCTRL_GEN_GCLK0 |
                        GCLK_CLKCTRL_CLKEN;
    timeout = 50000;
    while (GCLK->STATUS.bit.SYNCBUSY && --timeout);

    PORT->Group[1].PINCFG[22].bit.PMUXEN = 1;
    PORT->Group[1].PINCFG[23].bit.PMUXEN = 1;
    PORT->Group[1].PMUX[22 >> 1].reg = PORT_PMUX_PMUXE_D | PORT_PMUX_PMUXO_D;

    SERCOM5->USART.CTRLA.bit.SWRST = 1;
    timeout = 50000;
    while ((SERCOM5->USART.CTRLA.bit.SWRST || SERCOM5->USART.SYNCBUSY.bit.SWRST) && --timeout);

    SERCOM5->USART.CTRLA.reg = SERCOM_USART_CTRLA_MODE_USART_INT_CLK |
                               SERCOM_USART_CTRLA_DORD |
                               SERCOM_USART_CTRLA_TXPO(1) |
                               SERCOM_USART_CTRLA_RXPO(3);

    uint64_t baud_reg = 65536ULL - ((65536ULL * 16ULL * (uint64_t)baudrate) / 48000000ULL);
    SERCOM5->USART.BAUD.reg = (uint16_t)baud_reg;

    SERCOM5->USART.CTRLB.reg = SERCOM_USART_CTRLB_TXEN | SERCOM_USART_CTRLB_RXEN;
    timeout = 50000;
    while (SERCOM5->USART.SYNCBUSY.bit.CTRLB && --timeout);

    SERCOM5->USART.CTRLA.bit.ENABLE = 1;
    timeout = 50000;
    while (SERCOM5->USART.SYNCBUSY.bit.ENABLE && --timeout);
}

static void uart_putc(char c) {
    uint32_t timeout = 50000;
    while (!(SERCOM5->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_DRE) && --timeout);
    if (timeout > 0) {
        SERCOM5->USART.DATA.reg = (uint16_t)c;
    }
}

static void uart_puts(const char *str) {
    const char *p = str;
    size_t len = 0;
    while (*p) {
        uart_putc(*p++);
        len++;
    }
    /* Mirror output to USB CDC Virtual COM port when terminal is open */
    if (len > 0 && usb_cdc_dtr(SerialUSB)) {
        usb_cdc_write(SerialUSB, (const uint8_t *)str, len);
    }
}

static void uart_put_uint(uint32_t val) {
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (val == 0) {
        uart_puts("0");
        return;
    }
    while (val > 0 && i > 0) {
        buf[--i] = (char)('0' + (val % 10));
        val /= 10;
    }
    uart_puts(&buf[i]);
}

static void log_event(const char *task_tag, const char *event_msg) {
    UBaseType_t uxEffPrio = uxTaskPriorityGet(NULL);
    uart_puts("[t=");
    uart_put_uint((uint32_t)xTaskGetTickCount());
    uart_puts(" ms] ");
    uart_puts(task_tag);
    uart_puts(" (EffPrio=");
    uart_put_uint((uint32_t)uxEffPrio);
    uart_puts("): ");
    uart_puts(event_msg);
    uart_puts(" | Counts(T1=");
    uart_put_uint(ulTask1Count);
    uart_puts(", T2=");
    uart_put_uint(ulTask2Count);
    uart_puts(", T3=");
    uart_put_uint(ulTask3Count);
    uart_puts(") | Pin1=");
    uart_puts(gpio_led_is_on() ? "ON\r\n" : "OFF\r\n");
}

/* -------------------------------------------------------------------------- */
/* FreeRTOS Tasks                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Task 1: Turn Pin 1 ON -> Actively wait 0.5s -> Yield.
 */
void vTaskLedOn(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            ulTask1Count++;
            gpio_led_on();
            log_event("Task1_LedOn ", "GPIO Pin 1 -> ON  (active wait 500ms)");

            /* Actively wait for 0.5s (500 ms) using CPU spin loop */
            TickType_t xStartTick = xTaskGetTickCount();
            const TickType_t xWaitTicks = pdMS_TO_TICKS(500);
            while ((xTaskGetTickCount() - xStartTick) < xWaitTicks) {
                __asm volatile("nop");
            }

            log_event("Task1_LedOn ", "500ms active wait done -> Give mutex & taskYIELD()");
            xSemaphoreGive(xLedMutex);
        }

        /* Yield the CPU */
        taskYIELD();
    }
}

/**
 * @brief Task 2: Turn Pin 1 OFF -> Task-delay 1.0s.
 */
void vTaskLedOff(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            ulTask2Count++;
            gpio_led_off();
            log_event("Task2_LedOff", "GPIO Pin 1 -> OFF (task-delay 1000ms)");

            /* Task-delay for 1.0s (1000 ms) while keeping the LED OFF */
            vTaskDelay(pdMS_TO_TICKS(1000));

            xSemaphoreGive(xLedMutex);
        }

        /* Yield / short delay so other tasks can acquire the mutex */
        #if (SCHEDULER_CASE == 0) || (SCHEDULER_CASE == 1)
        taskYIELD();
        #else
        vTaskDelay(pdMS_TO_TICKS(100));
        #endif
    }
}

/**
 * @brief Task 3: Transmit status message via Serial UART -> Task-delay 1.0s.
 */
void vTaskSerialStatus(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        ulTask3Count++;
        log_event("Task3_Serial", "UART Status Heartbeat (task-delay 1000ms)");

        /* Block / task-delay for 1.0 second (1000 ms) */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* -------------------------------------------------------------------------- */
/* Application Entry Point                                                    */
/* -------------------------------------------------------------------------- */
void app_init(void) {
    gpio_init();
    uart_init(115200);

    /* Create Synchronization Primitive:
     * Cases 2 & 4 use Binary Semaphore (NO priority inheritance).
     * Cases 0, 1, 3, & 5 use FreeRTOS Mutex (Priority Inheritance enabled). */
#if (SCHEDULER_CASE == 2) || (SCHEDULER_CASE == 4)
    xLedMutex = xSemaphoreCreateBinary();
    if (xLedMutex != NULL) {
        xSemaphoreGive(xLedMutex); /* Initialize binary semaphore to available */
    }
#else
    xLedMutex = xSemaphoreCreateMutex();
#endif

    if (xLedMutex != NULL) {
        /* Create Task 1 (Turn LED ON, actively wait 0.5s, yield) */
        xTaskCreate(
            vTaskLedOn,
            "Task_LedOn",
            STACK_SIZE_LED_TASKS,
            NULL,
            PRIORITY_TASK_LED_ON,
            &xHandleTaskLedOn
        );

        /* Create Task 2 (Turn LED OFF, task-delay 1.0s) */
        xTaskCreate(
            vTaskLedOff,
            "Task_LedOff",
            STACK_SIZE_LED_TASKS,
            NULL,
            PRIORITY_TASK_LED_OFF,
            &xHandleTaskLedOff
        );

        /* Create Task 3 (Serial UART Status, task-delay 1.0s) */
        xTaskCreate(
            vTaskSerialStatus,
            "Task_Serial",
            STACK_SIZE_SERIAL,
            NULL,
            PRIORITY_TASK_SERIAL,
            &xHandleTaskSerial
        );

        /* Start the FreeRTOS scheduler */
        vTaskStartScheduler();
    }

    for (;;);
}

/* Hook into Arduino SAMD cortex_handlers.c SysTick_Handler */
extern void xPortSysTickHandler(void);
int sysTickHook(void) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
    return 0;
}

#ifndef ARDUINO
int main(void) {
    SystemInit();
    app_init();
    while (1);
    return 0;
}
#else
void setup(void) {
    app_init();
}

void loop(void) {
}
#endif
