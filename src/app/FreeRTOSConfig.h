/*
 * FreeRTOS V10.x Configuration for ATSAMD21G18A (Arduino MKR Zero)
 * Architecture: ARM Cortex-M0+ (ARMv6-M)
 * Compiler: GCC
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/*-----------------------------------------------------------
 * Application specific definitions.
 *----------------------------------------------------------*/

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 48000000 ) /* SAMD21 runs at 48 MHz */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )        /* 1 ms tick */
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )          /* Words (4 bytes each) */
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 12 * 1024 ) )   /* 12 KB heap in 32 KB SRAM */
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_TRACE_FACILITY                0
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* Mutexes and Synchronization */
#define configUSE_MUTEXES                       1   /* REQUIRED: Enables xSemaphoreCreateMutex() */
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1

/* Memory Allocation */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         1

/* Software Timers */
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               ( 2 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* Cortex-M0/M0+ Specific interrupt configuration */
#define configKERNEL_INTERRUPT_PRIORITY         ( 3 ) /* Lowest priority on Cortex-M0 (values 0-3) */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( 1 )

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1   /* REQUIRED: Enables vTaskDelay() */
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* Map FreeRTOS port interrupt handlers to CMSIS standard vector names */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
