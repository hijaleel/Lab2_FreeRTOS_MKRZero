/**
 * ============================================================================
 * Lab 2: FreeRTOS on Arduino MKR Zero (Atmel ATSAMD21G18A / ARM Cortex-M0+)
 * 
 * Requirements:
 * - 3 FreeRTOS tasks, each with its own priority determined by a const int.
 * - 1 FreeRTOS Mutex managing access to a shared GPIO pin (Pin 1).
 * - Task 1 (LedOn): Turns Pin 1 ON, actively waits for 0.5s, yields.
 * - Task 2 (LedOff): Turns Pin 1 OFF, task-delays for 1.0s.
 * - Task 3 (SerialStatus): Prints status message via Serial UART, task-delays for 1.0s.
 * ============================================================================
 */

#include <Arduino.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* -------------------------------------------------------------------------- */
/* Hardware Configuration                                                     */
/* -------------------------------------------------------------------------- */
const int LED_PIN = 1; /* Pin 1 connected to external LED */

/* -------------------------------------------------------------------------- */
/* Task Priorities (Each task has its own distinct priority via const int)    */
/* -------------------------------------------------------------------------- */
/*
 * Priority Design:
 * - PRIORITY_TASK_LED_OFF (3): Highest priority. When its 1-second delay 
 *   elapses, it immediately preempts lower-priority tasks to acquire the mutex 
 *   and turn the LED off.
 * - PRIORITY_TASK_SERIAL (2): Medium priority. Preempts the active-wait loop 
 *   of Task_LedOn every second to print status over UART.
 * - PRIORITY_TASK_LED_ON (1): Lowest priority. It actively waits for 0.5s and 
 *   yields, allowing higher-priority tasks to run when they wake up.
 */
const int PRIORITY_TASK_LED_OFF = 3;
const int PRIORITY_TASK_SERIAL  = 2;
const int PRIORITY_TASK_LED_ON  = 1;

/* -------------------------------------------------------------------------- */
/* Task Stack Sizes                                                           */
/* -------------------------------------------------------------------------- */
const configSTACK_DEPTH_TYPE STACK_SIZE_LED_TASKS = 128;
const configSTACK_DEPTH_TYPE STACK_SIZE_SERIAL    = 256;

/* -------------------------------------------------------------------------- */
/* FreeRTOS Handles                                                           */
/* -------------------------------------------------------------------------- */
SemaphoreHandle_t xLedMutex = NULL;

TaskHandle_t xHandleTaskLedOn   = NULL;
TaskHandle_t xHandleTaskLedOff  = NULL;
TaskHandle_t xHandleTaskSerial  = NULL;

/* -------------------------------------------------------------------------- */
/* Task 1: Turn LED ON -> Actively wait 0.5s -> Yield                         */
/* -------------------------------------------------------------------------- */
void vTaskLedOn(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Acquire mutex before accessing the shared GPIO pin
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            // Turn GPIO pin ON
            digitalWrite(LED_PIN, HIGH);

            // Actively wait for 0.5 seconds (500 ms) using CPU spin loop
            TickType_t xStartTick = xTaskGetTickCount();
            const TickType_t xWaitTicks = pdMS_TO_TICKS(500);
            while ((xTaskGetTickCount() - xStartTick) < xWaitTicks) {
                // Active / busy-wait loop (keeps CPU active)
                __asm volatile("nop");
            }

            // Release mutex after finishing the active wait
            xSemaphoreGive(xLedMutex);
        }

        // Yield CPU to let other ready tasks run
        taskYIELD();
    }
}

/* -------------------------------------------------------------------------- */
/* Task 2: Turn LED OFF -> Task-delay 1.0s                                    */
/* -------------------------------------------------------------------------- */
void vTaskLedOff(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Acquire mutex before accessing the shared GPIO pin
        if (xSemaphoreTake(xLedMutex, portMAX_DELAY) == pdTRUE) {
            // Turn GPIO pin OFF
            digitalWrite(LED_PIN, LOW);

            // Release mutex
            xSemaphoreGive(xLedMutex);
        }

        // Block / task-delay for 1.0 second (1000 ms)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* -------------------------------------------------------------------------- */
/* Task 3: Print Status via Serial UART -> Task-delay 1.0s                    */
/* -------------------------------------------------------------------------- */
void vTaskSerialStatus(void *pvParameters) {
    (void) pvParameters;

    uint32_t ulMessageCount = 0;

    for (;;) {
        ulMessageCount++;

        // Output status message over Serial UART (USB CDC on MKRZero)
        Serial.print("[FreeRTOS Status] Heartbeat #");
        Serial.print(ulMessageCount);
        Serial.print(" | Uptime (ticks): ");
        Serial.print((unsigned long)xTaskGetTickCount());
        Serial.print(" | LED Pin: ");
        Serial.println(digitalRead(LED_PIN) ? "ON" : "OFF");

        // Block / task-delay for 1.0 second (1000 ms)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* -------------------------------------------------------------------------- */
/* Main Setup & Scheduler Initialization                                      */
/* -------------------------------------------------------------------------- */
void setup() {
    // Initialize Serial UART
    Serial.begin(115200);
    // Optional: wait a moment for serial monitor connection if needed
    // while (!Serial && millis() < 3000);

    Serial.println("--- FreeRTOS Lab 2: Arduino MKR Zero Initializing ---");

    // Configure Pin 1 as digital output
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // Create the FreeRTOS Mutex to guard access to LED Pin 1
    xLedMutex = xSemaphoreCreateMutex();

    if (xLedMutex != NULL) {
        // 1. Create Task 1 (Turn LED ON, actively wait 0.5s, yield)
        xTaskCreate(
            vTaskLedOn,             /* Task function pointer */
            "Task_LedOn",           /* Descriptive name */
            STACK_SIZE_LED_TASKS,   /* Stack depth in words */
            NULL,                   /* Parameter passed to task */
            PRIORITY_TASK_LED_ON,   /* Task priority (const int = 1) */
            &xHandleTaskLedOn       /* Task handle */
        );

        // 2. Create Task 2 (Turn LED OFF, task-delay 1s)
        xTaskCreate(
            vTaskLedOff,            /* Task function pointer */
            "Task_LedOff",          /* Descriptive name */
            STACK_SIZE_LED_TASKS,   /* Stack depth in words */
            NULL,                   /* Parameter passed to task */
            PRIORITY_TASK_LED_OFF,  /* Task priority (const int = 3) */
            &xHandleTaskLedOff      /* Task handle */
        );

        // 3. Create Task 3 (Serial UART Status, task-delay 1s)
        xTaskCreate(
            vTaskSerialStatus,      /* Task function pointer */
            "Task_Serial",          /* Descriptive name */
            STACK_SIZE_SERIAL,      /* Stack depth in words */
            NULL,                   /* Parameter passed to task */
            PRIORITY_TASK_SERIAL,   /* Task priority (const int = 2) */
            &xHandleTaskSerial      /* Task handle */
        );

        Serial.println("Tasks and Mutex created successfully. Starting scheduler...");

        // Start the FreeRTOS scheduler
        vTaskStartScheduler();
    } else {
        Serial.println("ERROR: Failed to create Mutex. Insufficient FreeRTOS heap.");
    }

    // Should never reach here unless heap is depleted
    for (;;);
}

void loop() {
    // Empty: Execution handled entirely by FreeRTOS tasks
}
