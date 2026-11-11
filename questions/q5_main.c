/**
 * ============================================================================
 * Lab 2 - Question 5: Inter-Task Communication (Message Queue & Semaphore)
 * Using pure CMSIS ("sam.h") without Arduino.h
 *
 * Replaces the Mutex with:
 *   - FreeRTOS Message Queue (xQueueCreate / xQueueSend / xQueueReceive)
 *     passing a structured LedMessage_t payload between Task 1 and Task 2
 *     (or Binary Semaphore xSemaphoreCreateBinary when USE_BINARY_SEMAPHORE=1).
 * ============================================================================
 */

#include "sam.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* FreeRTOS Kernel Includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define PIN1_MASK           PORT_PA23        /* Header Pin 1 = PA23 */
#define ONBOARD_LED_MASK    (1UL << 8)       /* Built-in LED = PB08 */

/* Inter-Task Communication Message Structure (for Queue IPC) */
typedef struct {
    uint8_t    ucLedState;       /* 0 = LED OFF, 1 = LED ON */
    uint8_t    ucSenderTaskId;   /* 0 = Init, 1 = Task1_LedOn, 2 = Task2_LedOff */
    uint32_t   ulSequenceNum;    /* Monotonic IPC message sequence number */
    TickType_t xTimestampTicks;  /* Kernel tick timestamp when message was sent */
} LedMessage_t;

/* Distinct Task Priorities determined by const int */
const int PRIORITY_TASK_LED_ON  = 1;
const int PRIORITY_TASK_LED_OFF = 3;
const int PRIORITY_TASK_SERIAL  = 2;

#define STACK_SIZE_LED_TASKS    ( ( configSTACK_DEPTH_TYPE ) 256 )
#define STACK_SIZE_SERIAL       ( ( configSTACK_DEPTH_TYPE ) 256 )

/* FreeRTOS Message Queue Handle replacing the Mutex */
QueueHandle_t xLedQueue = NULL;

TaskHandle_t xHandleTaskLedOn   = NULL;
TaskHandle_t xHandleTaskLedOff  = NULL;
TaskHandle_t xHandleTaskSerial  = NULL;

volatile uint32_t ulTask1Count  = 0;
volatile uint32_t ulTask2Count  = 0;
volatile uint32_t ulTask3Count  = 0;
volatile uint32_t ulQueueMsgSeq = 0;

volatile uint32_t ulTask1BlockedIPC   = 0;
volatile uint32_t ulTask2BlockedIPC   = 0;
volatile uint32_t ulTask2BlockedDelay = 0;
volatile uint32_t ulTask3BlockedDelay = 0;
volatile uint32_t ulTotalBlockedEvents = 0;
volatile uint32_t ulIdleHookCalls = 0;

extern uint8_t SerialUSB[];
extern size_t usb_cdc_write(void *this_ptr, const uint8_t *buf, size_t len) __asm__("_ZN7Serial_5writeEPKhj");
extern bool usb_cdc_dtr(void *this_ptr) __asm__("_ZN7Serial_3dtrEv");

void vTraceTaskBlockedOnMutex(void *pxQueue) {
    if ((pxQueue != NULL) && (pxQueue == (void *)xLedQueue)) {
        TaskHandle_t xCur = xTaskGetCurrentTaskHandle();
        if (xCur == xHandleTaskLedOn) {
            ulTask1BlockedIPC++;
        } else if (xCur == xHandleTaskLedOff) {
            ulTask2BlockedIPC++;
        }
        ulTotalBlockedEvents++;
    }
}

void vTraceTaskBlockedOnDelay(void) {
    TaskHandle_t xCur = xTaskGetCurrentTaskHandle();
    if (xCur == xHandleTaskLedOff) {
        ulTask2BlockedDelay++;
    } else if (xCur == xHandleTaskSerial) {
        ulTask3BlockedDelay++;
    }
    ulTotalBlockedEvents++;
}

void vApplicationIdleHook(void) {
    ulIdleHookCalls++;
}

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
    uart_puts(",IPC=Queue#");
    uart_put_uint(ulQueueMsgSeq);
    uart_puts("): ");
    uart_puts(event_msg);
    uart_puts(" | Runs(T1=");
    uart_put_uint(ulTask1Count);
    uart_puts(",T2=");
    uart_put_uint(ulTask2Count);
    uart_puts(",T3=");
    uart_put_uint(ulTask3Count);
    uart_puts(") | Blocked(T1_ipc=");
    uart_put_uint(ulTask1BlockedIPC);
    uart_puts(",T2_ipc=");
    uart_put_uint(ulTask2BlockedIPC);
    uart_puts(",T2_dly=");
    uart_put_uint(ulTask2BlockedDelay);
    uart_puts(",T3_dly=");
    uart_put_uint(ulTask3BlockedDelay);
    uart_puts(") | Pin1=");
    uart_puts(gpio_led_is_on() ? "ON\r\n" : "OFF\r\n");
}

void vTaskLedOn(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        LedMessage_t xRxMsg;
        if (xQueueReceive(xLedQueue, &xRxMsg, portMAX_DELAY) == pdPASS) {
            ulTask1Count++;
            gpio_led_on();
            log_event("Task1_LedOn ", "xQueueReceive() OK -> GPIO Pin 1 ON (active wait 500ms)");

            TickType_t xStartTick = xTaskGetTickCount();
            const TickType_t xWaitTicks = pdMS_TO_TICKS(500);
            while ((xTaskGetTickCount() - xStartTick) < xWaitTicks) {
                __asm volatile("nop");
            }

            LedMessage_t xTxMsg = {
                .ucLedState      = 1U,
                .ucSenderTaskId  = 1U,
                .ulSequenceNum   = ++ulQueueMsgSeq,
                .xTimestampTicks = xTaskGetTickCount()
            };
            log_event("Task1_LedOn ", "500ms active wait done -> xQueueSend() & taskYIELD()");
            xQueueSend(xLedQueue, &xTxMsg, portMAX_DELAY);
        }
        taskYIELD();
    }
}

void vTaskLedOff(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        LedMessage_t xRxMsg;
        if (xQueueReceive(xLedQueue, &xRxMsg, portMAX_DELAY) == pdPASS) {
            ulTask2Count++;
            gpio_led_off();
            log_event("Task2_LedOff", "xQueueReceive() OK -> GPIO Pin 1 OFF (task-delay 1000ms)");

            vTaskDelay(pdMS_TO_TICKS(1000));

            LedMessage_t xTxMsg = {
                .ucLedState      = 0U,
                .ucSenderTaskId  = 2U,
                .ulSequenceNum   = ++ulQueueMsgSeq,
                .xTimestampTicks = xTaskGetTickCount()
            };
            xQueueSend(xLedQueue, &xTxMsg, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void vTaskSerialStatus(void *pvParameters) {
    (void) pvParameters;
    for (;;) {
        ulTask3Count++;
        log_event("Task3_Serial", "UART Status Heartbeat (task-delay 1000ms)");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_init(void) {
    gpio_init();
    uart_init(115200);

    xLedQueue = xQueueCreate(1, sizeof(LedMessage_t));
    if (xLedQueue != NULL) {
        LedMessage_t xInitMsg = {0U, 0U, 0U, 0U};
        if (xQueueSend(xLedQueue, &xInitMsg, 0) == pdPASS) {
            xTaskCreate(vTaskLedOn,        "Task_LedOn",  STACK_SIZE_LED_TASKS, NULL, PRIORITY_TASK_LED_ON,  &xHandleTaskLedOn);
            xTaskCreate(vTaskLedOff,       "Task_LedOff", STACK_SIZE_LED_TASKS, NULL, PRIORITY_TASK_LED_OFF, &xHandleTaskLedOff);
            xTaskCreate(vTaskSerialStatus, "Task_Serial", STACK_SIZE_SERIAL,    NULL, PRIORITY_TASK_SERIAL,  &xHandleTaskSerial);
            vTaskStartScheduler();
        }
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
