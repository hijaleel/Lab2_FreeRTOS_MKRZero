/**
 * ============================================================================
 * Lab 2 - Question 2: FreeRTOS Application on ATSAMD21G18A (Arduino MKR Zero)
 * Using pure CMSIS ("sam.h") without Arduino.h
 *
 * Task Specification (Question 2):
 *   - Task 1 (vTaskLedOn):      Turn GPIO Pin 1 (PA23) ON -> Actively wait 0.5s -> Yield.
 *   - Task 2 (vTaskLedOff):     Turn GPIO Pin 1 (PA23) OFF -> Task-delay 1.0s.
 *   - Task 3 (vTaskSerialStatus): Transmit status message via Serial UART -> Task-delay 1.0s.
 *   - Each task has its own priority determined by a const int.
 *   - Access to the GPIO pin is managed using a FreeRTOS Mutex (xSemaphoreCreateMutex).
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

/* Hardware Pin Definitions (ATSAMD21G18A on Arduino MKR Zero) */
#define PIN1_MASK           PORT_PA23        /* External Header Pin 1 = PA23 */
#define ONBOARD_LED_MASK    (1UL << 8)       /* Built-in LED ("L")    = PB08 */

/* Distinct Task Priorities determined by const int */
const int PRIORITY_TASK_LED_ON  = 1;
const int PRIORITY_TASK_LED_OFF = 3;
const int PRIORITY_TASK_SERIAL  = 2;

/* Task Stack Sizes (in words) */
#define STACK_SIZE_LED_TASKS    ( ( configSTACK_DEPTH_TYPE ) 256 )
#define STACK_SIZE_SERIAL       ( ( configSTACK_DEPTH_TYPE ) 256 )

/* FreeRTOS Mutex Handle protecting GPIO Pin 1 */
SemaphoreHandle_t xLedMutex = NULL;

/* Task Execution Counters */
volatile uint32_t ulTask1Count = 0;
volatile uint32_t ulTask2Count = 0;
volatile uint32_t ulTask3Count = 0;

/* USB CDC Symbol Bridge (mirrors SERCOM5 UART to USB Virtual COM port) */
extern uint8_t SerialUSB[];
extern size_t usb_cdc_write(void *this_ptr, const uint8_t *buf, size_t len) __asm__("_ZN7Serial_5writeEPKhj");
extern bool usb_cdc_dtr(void *this_ptr) __asm__("_ZN7Serial_3dtrEv");

/* Low-Level GPIO & SERCOM5 UART Functions (Pure CMSIS / sam.h) */
static void gpio_init(void) {
    PORT->Group[0].DIRSET.reg = PIN1_MASK;
    PORT->Group[0].OUTCLR.reg = PIN1_MASK;
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
    if (len > 0 && usb_cdc_dtr(SerialUSB)) {
        usb_cdc_write(SerialUSB, (const uint8_t *)str, len);
    }
}

/* Task 1: Turn Pin 1 ON -> Actively wait 0.5s -> Yield */
void vTaskLedOn(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            ulTask1Count++;
            gpio_led_on();
            uart_puts("Task1_LedOn:  GPIO Pin 1 -> ON  (active wait 500ms)\r\n");

            TickType_t xStartTick = xTaskGetTickCount();
            const TickType_t xWaitTicks = pdMS_TO_TICKS(500);
            while ((xTaskGetTickCount() - xStartTick) < xWaitTicks) {
                __asm volatile("nop");
            }

            xSemaphoreGive(xLedMutex);
        }
        taskYIELD();
    }
}

/* Task 2: Turn Pin 1 OFF -> Task-delay 1.0s */
void vTaskLedOff(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            ulTask2Count++;
            gpio_led_off();
            uart_puts("Task2_LedOff: GPIO Pin 1 -> OFF (task-delay 1000ms)\r\n");

            vTaskDelay(pdMS_TO_TICKS(1000));
            xSemaphoreGive(xLedMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Task 3: Transmit status message via Serial UART -> Task-delay 1.0s */
void vTaskSerialStatus(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        ulTask3Count++;
        if (gpio_led_is_on()) {
            uart_puts("Task3_Serial: Status = Pin 1 is ON  (delay 1000ms)\r\n");
        } else {
            uart_puts("Task3_Serial: Status = Pin 1 is OFF (delay 1000ms)\r\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_init(void) {
    gpio_init();
    uart_init(115200);

    xLedMutex = xSemaphoreCreateMutex();
    if (xLedMutex != NULL) {
        xTaskCreate(vTaskLedOn,        "Task_LedOn",  STACK_SIZE_LED_TASKS, NULL, PRIORITY_TASK_LED_ON,  NULL);
        xTaskCreate(vTaskLedOff,       "Task_LedOff", STACK_SIZE_LED_TASKS, NULL, PRIORITY_TASK_LED_OFF, NULL);
        xTaskCreate(vTaskSerialStatus, "Task_Serial", STACK_SIZE_SERIAL,    NULL, PRIORITY_TASK_SERIAL,  NULL);
        vTaskStartScheduler();
    }
    for (;;);
}

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
void setup(void) { app_init(); }
void loop(void) {}
#endif
