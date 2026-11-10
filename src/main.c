/**
 * ============================================================================
 * Lab 2: FreeRTOS on ATSAMD21G18A (Arduino MKR Zero / ARM Cortex-M0+)
 * Using pure CMSIS ("sam.h") without Arduino.h
 *
 * Requirements:
 * - 3 FreeRTOS tasks, each with its own distinct priority level (const int).
 * - 1 FreeRTOS Mutex managing access to a shared GPIO pin (Pin 1 = PA23).
 * - Task 1 (LedOn): Turns Pin 1 ON, actively waits for 0.5s, yields.
 * - Task 2 (LedOff): Turns Pin 1 OFF, task-delays for 1.0s.
 * - Task 3 (SerialStatus): Transmits status message via Serial UART, task-delays for 1.0s.
 * ============================================================================
 */

#include "sam.h"
#include <stdint.h>
#include <stdbool.h>

/* FreeRTOS Kernel Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* -------------------------------------------------------------------------- */
/* Hardware Pin Definitions (ATSAMD21G18A on Arduino MKR Zero)                */
/* - Digital Pin 1 on MKR Zero header maps to PORTA Pin 23 (PA23)             */
/* - Builtin onboard LED (labeled "L") maps to PORTB Pin 8 (PB08)             */
/* We drive BOTH so you can see it blink on Pin 1 AND on the onboard LED!     */
/* -------------------------------------------------------------------------- */
#define PIN1_MASK           PORT_PA23        /* Pin 1 = PA23 (1UL << 23) */
#define ONBOARD_LED_MASK    (1UL << 8)       /* Builtin LED = PB08 */

/* -------------------------------------------------------------------------- */
/* Task Priorities (Each task has its own distinct priority via const int)    */
/* -------------------------------------------------------------------------- */
/*
 * Priority Structure:
 * - PRIORITY_TASK_LED_OFF (3): Highest priority.
 * - PRIORITY_TASK_SERIAL  (2): Medium priority.
 * - PRIORITY_TASK_LED_ON  (1): Lowest priority.
 */
const int PRIORITY_TASK_LED_OFF = 3;
const int PRIORITY_TASK_SERIAL  = 2;
const int PRIORITY_TASK_LED_ON  = 1;

/* -------------------------------------------------------------------------- */
/* Task Stack Sizes (in words)                                                */
/* -------------------------------------------------------------------------- */
#define STACK_SIZE_LED_TASKS    ( ( configSTACK_DEPTH_TYPE ) 128 )
#define STACK_SIZE_SERIAL       ( ( configSTACK_DEPTH_TYPE ) 256 )

/* -------------------------------------------------------------------------- */
/* FreeRTOS Synchronization Handles                                           */
/* -------------------------------------------------------------------------- */
SemaphoreHandle_t xLedMutex = NULL;

TaskHandle_t xHandleTaskLedOn   = NULL;
TaskHandle_t xHandleTaskLedOff  = NULL;
TaskHandle_t xHandleTaskSerial  = NULL;

/* -------------------------------------------------------------------------- */
/* Low-Level GPIO & UART Helper Functions (Pure CMSIS / sam.h)                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure Pin 1 (PA23) and Onboard LED (PB08) as digital outputs.
 */
static void gpio_init(void) {
    /* Set Pin 1 (PA23, PORTA) as output */
    PORT->Group[0].DIRSET.reg = PIN1_MASK;
    PORT->Group[0].OUTCLR.reg = PIN1_MASK;

    /* Set Onboard LED (PB08, PORTB) as output */
    PORT->Group[1].DIRSET.reg = ONBOARD_LED_MASK;
    PORT->Group[1].OUTCLR.reg = ONBOARD_LED_MASK;
}

/**
 * @brief Drive Pin 1 (PA23) and Onboard LED (PB08) HIGH (LED ON).
 */
static inline void gpio_led_on(void) {
    PORT->Group[0].OUTSET.reg = PIN1_MASK;
    PORT->Group[1].OUTSET.reg = ONBOARD_LED_MASK;
}

/**
 * @brief Drive Pin 1 (PA23) and Onboard LED (PB08) LOW (LED OFF).
 */
static inline void gpio_led_off(void) {
    PORT->Group[0].OUTCLR.reg = PIN1_MASK;
    PORT->Group[1].OUTCLR.reg = ONBOARD_LED_MASK;
}

/**
 * @brief Read current output state of Pin 1 (PA23).
 */
static inline bool gpio_led_is_on(void) {
    return (PORT->Group[0].OUT.reg & PIN1_MASK) != 0;
}

/**
 * @brief Initialize SERCOM5 for Serial UART output.
 */
static void uart_init(uint32_t baudrate) {
    (void)baudrate;
    /* Enable APBC clock for SERCOM5 */
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;
}

/**
 * @brief Send a single character over Serial UART.
 */
static void uart_putc(char c) {
    /* If SERCOM5 is enabled, transmit; otherwise non-blocking */
    if (SERCOM5->USART.CTRLA.bit.ENABLE) {
        uint32_t timeout = 5000;
        while (!(SERCOM5->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_DRE) && --timeout);
        if (timeout > 0) {
            SERCOM5->USART.DATA.reg = (uint16_t)c;
        }
    }
}

/**
 * @brief Send a null-terminated string over Serial UART.
 */
static void uart_puts(const char *str) {
    while (*str) {
        uart_putc(*str++);
    }
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
        /* Acquire mutex guarding the shared GPIO pin */
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            /* Turn Pin 1 ON (PA23 HIGH) and Onboard LED ON */
            gpio_led_on();

            /* Actively wait for 0.5s (500 ms) using CPU spin loop */
            TickType_t xStartTick = xTaskGetTickCount();
            const TickType_t xWaitTicks = pdMS_TO_TICKS(500);
            while ((xTaskGetTickCount() - xStartTick) < xWaitTicks) {
                __asm volatile("nop"); /* Active wait / CPU spin */
            }

            /* Release mutex after completing active wait */
            xSemaphoreGive(xLedMutex);
        }

        /* Yield the CPU to allow other ready tasks to execute */
        taskYIELD();
    }
}

/**
 * @brief Task 2: Turn Pin 1 OFF -> Task-delay 1.0s.
 */
void vTaskLedOff(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        /* Acquire mutex guarding the shared GPIO pin */
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            /* Turn Pin 1 OFF (PA23 LOW) and Onboard LED OFF */
            gpio_led_off();

            /* Hold mutex and delay for 1.0s (1000 ms) so the LED stays visibly OFF */
            vTaskDelay(pdMS_TO_TICKS(1000));

            /* Release mutex after the 1.0s OFF period */
            xSemaphoreGive(xLedMutex);
        }

        /* Small delay to allow Task 1 to acquire mutex and turn LED ON */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Task 3: Transmit status message via Serial UART -> Task-delay 1.0s.
 */
void vTaskSerialStatus(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        /* Print status message via Serial UART */
        uart_puts("[FreeRTOS Status] Heartbeat | Pin 1 State: ");
        if (gpio_led_is_on()) {
            uart_puts("ON\r\n");
        } else {
            uart_puts("OFF\r\n");
        }

        /* Block / task-delay for 1.0 second (1000 ms) */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* -------------------------------------------------------------------------- */
/* Application Entry Point                                                    */
/* -------------------------------------------------------------------------- */
void app_init(void) {
    /* Initialize peripherals using pure sam.h registers */
    gpio_init();
    uart_init(115200);

    uart_puts("\r\n=== FreeRTOS Lab 2: SAMD21 (sam.h) Initialized ===\r\n");

    /* Create the FreeRTOS Mutex */
    xLedMutex = xSemaphoreCreateMutex();

    if (xLedMutex != NULL) {
        /* 1. Create Task 1 (Turn LED ON, actively wait 0.5s, yield) */
        xTaskCreate(
            vTaskLedOn,
            "Task_LedOn",
            STACK_SIZE_LED_TASKS,
            NULL,
            PRIORITY_TASK_LED_ON,
            &xHandleTaskLedOn
        );

        /* 2. Create Task 2 (Turn LED OFF, task-delay 1.0s) */
        xTaskCreate(
            vTaskLedOff,
            "Task_LedOff",
            STACK_SIZE_LED_TASKS,
            NULL,
            PRIORITY_TASK_LED_OFF,
            &xHandleTaskLedOff
        );

        /* 3. Create Task 3 (Serial UART Status, task-delay 1.0s) */
        xTaskCreate(
            vTaskSerialStatus,
            "Task_Serial",
            STACK_SIZE_SERIAL,
            NULL,
            PRIORITY_TASK_SERIAL,
            &xHandleTaskSerial
        );

        uart_puts("Tasks and Mutex created. Starting scheduler...\r\n");

        /* Start the FreeRTOS scheduler */
        vTaskStartScheduler();
    } else {
        uart_puts("ERROR: Failed to allocate Mutex from FreeRTOS heap.\r\n");
    }

    /* Should never reach here */
    for (;;);
}

/* Hook into Arduino SAMD cortex_handlers.c to route SysTick to FreeRTOS */
extern void xPortSysTickHandler(void);
int sysTickHook(void) {
    xPortSysTickHandler();
    return 0;
}

/* Compatible entry point for both bare-metal CMSIS and framework environments */
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
    /* Handled by FreeRTOS scheduler */
}
#endif
