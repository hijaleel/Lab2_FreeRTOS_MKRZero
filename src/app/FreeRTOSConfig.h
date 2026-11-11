/*
 * FreeRTOS V10.5.1 Configuration for ATSAMD21G18A (Arduino MKR Zero)
 * Architecture: ARM Cortex-M0+ (ARMv6-M)
 * Compiler: GCC
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/*
 * SCHEDULER_CASE selects the scheduler configuration for Lab 2:
 *   0: Same Priority (1, 1, 1) - Run to Completion (configUSE_PREEMPTION = 0)
 *   1: Case (a) Task1=3, Task2=2, Task3=1 - Run to Completion (configUSE_PREEMPTION = 0)
 *   2: Case (b) No Priority Inheritance (configUSE_PREEMPTION = 1, Binary Semaphore)
 *   3: Case (c) Priority Inheritance Enabled (configUSE_PREEMPTION = 1, Mutex)
 *   4: Case (b-inv) Priority Inversion Demo (Preemptive, Binary Semaphore, No Inheritance)
 *   5: Distinct Priorities (LedOff=3, Serial=2, LedOn=1) - Preemptive + Priority Inheritance
 */
#ifndef SCHEDULER_CASE
#define SCHEDULER_CASE 5
#endif

/*-----------------------------------------------------------
 * Application specific definitions.
 *----------------------------------------------------------*/

#if (SCHEDULER_CASE == 0) || (SCHEDULER_CASE == 1)
    #define configUSE_PREEMPTION                0   /* Run to completion (cooperative) */
    #define configUSE_TIME_SLICING              0
#else
    #define configUSE_PREEMPTION                1   /* Preemptive scheduler */
    #define configUSE_TIME_SLICING              1
#endif

/* Question 4: Enable IDLE Hook and Kernel Debug/RunTime Trace Macros */
#define configUSE_IDLE_HOOK                     1
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 48000000 ) /* SAMD21 runs at 48 MHz */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )        /* 1 ms tick */
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )          /* Words (4 bytes each) */
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 12 * 1024 ) )   /* 12 KB heap in 32 KB SRAM */
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_TRACE_FACILITY                1
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1

/* Mutexes and Synchronization */
#define configUSE_MUTEXES                       1   /* Enables xSemaphoreCreateMutex() & Priority Inheritance */
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1

/* Memory Allocation */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0

/* Software Timers */
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               ( 2 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* Cortex-M0/M0+ Specific interrupt configuration */
#define configKERNEL_INTERRUPT_PRIORITY         ( 3 )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( 1 )

/* Optional API functions included */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* Map FreeRTOS port interrupt handlers to CMSIS standard vector names */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler

/*-----------------------------------------------------------
 * Question 4: Kernel-Level Debug/RunTime Trace Macros
 * Tracks every exact instance in which a task enters the Blocked state
 * (either blocking on a mutex/semaphore or blocking on vTaskDelay).
 *----------------------------------------------------------*/
#ifndef __ASSEMBLER__
extern void vTraceTaskBlockedOnMutex( void *pxQueue );
extern void vTraceTaskBlockedOnDelay( void );
#endif

#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue )   vTraceTaskBlockedOnMutex( ( void * )( pxQueue ) )
#define traceTASK_DELAY()                           vTraceTaskBlockedOnDelay()

#endif /* FREERTOS_CONFIG_H */
