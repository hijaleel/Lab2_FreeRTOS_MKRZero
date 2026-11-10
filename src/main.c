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
/* Digital Pin 1 maps to PORTA Pin 23 (PA23)                                  */
/* -------------------------------------------------------------------------- */
#define LED_PIN_MASK        PORT_PA23       /* PA23 bitmask (1UL << 23) */
#define LED_PORT_GROUP      (PORT->Group[0]) /* Group 0 = PORTA */

/* -------------------------------------------------------------------------- */
/* Task Priorities (Each task has its own distinct priority via const int)    */
/* -------------------------------------------------------------------------- */
/*
 * Priority Structure:
 * - PRIORITY_TASK_LED_OFF (3): Highest priority. Preempts lower-priority tasks 
 *   as soon as its 1-second delay elapses to take the mutex and turn the LED off.
 * - PRIORITY_TASK_SERIAL  (2): Medium priority. Preempts the active-waiting loop 
 *   of Task_LedOn every second to transmit UART status.
 * - PRIORITY_TASK_LED_ON  (1): Lowest priority. It actively waits (spins) for 0.5s 
 *   and yields, allowing higher-priority tasks to run whenever unblocked.
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
/* Low-Level GPIO & UART Helper Functions (Pure CMSIS / sam.h)               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure Pin 1 (PA23) as a digital output.
 */
static void gpio_init(void) {
    /* Set PA23 direction to output */
    LED_PORT_GROUP.DIRSET.reg = LED_PIN_MASK;
    /* Initialize pin LOW (LED OFF) */
    LED_PORT_GROUP.OUTCLR.reg = LED_PIN_MASK;
}

/**
 * @brief Drive Pin 1 (PA23) HIGH (LED ON).
 */
static inline void gpio_led_on(void) {
    LED_PORT_GROUP.OUTSET.reg = LED_PIN_MASK;
}

/**
 * @brief Drive Pin 1 (PA23) LOW (LED OFF).
 */
static inline void gpio_led_off(void) {
    LED_PORT_GROUP.OUTCLR.reg = LED_PIN_MASK;
}

/**
 * @brief Read current output state of Pin 1 (PA23).
 */
static inline bool gpio_led_is_on(void) {
    return (LED_PORT_GROUP.OUT.reg & LED_PIN_MASK) != 0;
}

/**
 * @brief Initialize SERCOM5 for Serial UART output on PB22(RX) / PB23(TX).
 */
static void uart_init(uint32_t baudrate) {
    /* Enable APBC clock for SERCOM5 */
    PM->APBCMASK.reg |= PM_APBCMASK_SERCOM5;

    /* Feed GCLK Generator 0 (48MHz) to SERCOM5 */
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(SERCOM5_GCLK_ID_CORE) |
                        GCLK_CLKCTRL_GEN_GCLK0 |
                        GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY);

    /* Configure PB22 and PB23 pins for SERCOM5 peripheral mux */
    PORT->Group[1].PINCFG[22].bit.PMUXEN = 1;
    PORT->Group[1].PINCFG[23].bit.PMUXEN = 1;
    PORT->Group[1].PMUX[22 >> 1].reg = PORT_PMUX_PMUXE_D | PORT_PMUX_PMUXO_D;

    /* Software reset SERCOM5 */
    SERCOM5->USART.CTRLA.reg = SERCOM_USART_CTRLA_SWRST;
    while (SERCOM5->USART.CTRLA.bit.SWRST || SERCOM5->USART.SYNCBUSY.bit.SWRST);

    /* Configure USART mode: asynchronous, internal clock, LSB first */
    SERCOM5->USART.CTRLA.reg = SERCOM_USART_CTRLA_MODE_USART_INT_CLK |
                               SERCOM_USART_CTRLA_DORD |
                               SERCOM_USART_CTRLA_RXPO(0) | /* PB22 as PAD[0] RX */
                               SERCOM_USART_CTRLA_TXPO(0);  /* PB23 as PAD[1] TX */

    /* Enable transmitter */
    SERCOM5->USART.CTRLB.reg = SERCOM_USART_CTRLB_TXEN;
    while (SERCOM5->USART.SYNCBUSY.bit.CTRLB);

    /* Calculate baud rate value for asynchronous arithmetic mode */
    /* BAUD = 65536 * (1 - 16 * (baudrate / f_ref)) */
    uint64_t baud_val = 65536ULL * (48000000ULL - 16ULL * (uint64_t)baudrate) / 48000000ULL;
    SERCOM5->USART.BAUD.reg = (uint16_t)baud_val;

    /* Enable SERCOM5 */
    SERCOM5->USART.CTRLA.reg |= SERCOM_USART_CTRLA_ENABLE;
    while (SERCOM5->USART.SYNCBUSY.bit.ENABLE);
}

/**
 * @brief Send a single character over Serial UART.
 */
static void uart_putc(char c) {
    uint32_t timeout = 50000;
    while (!(SERCOM5->USART.INTFLAG.reg & SERCOM_USART_INTFLAG_DRE) && --timeout) {
        /* Wait for Data Register Empty */
    }
    if (timeout > 0) {
        SERCOM5->USART.DATA.reg = (uint16_t)c;
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
            /* Turn Pin 1 ON (PA23 HIGH) */
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
            /* Turn Pin 1 OFF (PA23 LOW) */
            gpio_led_off();

            /* Release mutex */
            xSemaphoreGive(xLedMutex);
        }

        /* Block / task-delay for 1.0 second (1000 ms) */
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief Task 3: Transmit status message via Serial UART -> Task-delay 1.0s.
 */
void vTaskSerialStatus(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        /* Print status message via Serial UART */
        uart_puts("[FreeRTOS Status] Heartbeat | Pin 1 (PA23) State: ");
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
