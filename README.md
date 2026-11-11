# Lab 2: FreeRTOS on Arduino MKR Zero (ATSAMD21G18A / ARM Cortex-M0+)

Repository for **Lab 2 (FreeRTOS Porting, Synchronization, Scheduler Performance & Blocked-Task Investigation)** on the **Arduino MKR Zero** (`ATSAMD21G18A`, ARM Cortex-M0+) using **GCC** and pure CMSIS (`sam.h`) register-level programming.

---

## Quick Links for Grading (Questions 1 – 4)

| Deliverable | File Link | Description |
| :--- | :--- | :--- |
| **C Application Program** | [`src/main.c`](./src/main.c) | Complete C application (`sam.h`) with 3 FreeRTOS tasks, mutex/semaphore synchronization on **Pin 1 (`PA23`)**, SERCOM5 UART driver, and Question 4 kernel trace hooks (`traceBLOCKING_ON_QUEUE_RECEIVE` & `traceTASK_DELAY`) + Idle Hook (`vApplicationIdleHook`). |
| **FreeRTOS Config** | [`include/FreeRTOSConfig.h`](./include/FreeRTOSConfig.h) | Customizable FreeRTOS configuration with `configUSE_PREEMPTION`, `configUSE_MUTEXES`, `configUSE_IDLE_HOOK`, and kernel trace macro definitions. |
| **Application Binaries** | [`binaries/firmware.bin`](./binaries/firmware.bin)<br>[`binaries/`](./binaries) | Compiled application binary (`firmware.bin` / `firmware.elf`) plus individual binaries for Same Priority (`q4_same_priorities_trace.bin`), Distinct Priorities (`q4_distinct_priorities_trace.bin`), and Cases (a), (b), (c). |
| **Demonstration Video (<5MB)** | [`demo_video.mp4`](./demo_video.mp4)<br>([`evidence/demo_video.mp4`](./evidence/demo_video.mp4)) | Video (`3.89 MB`, strictly `< 5 MB`) proving that the program built, loaded, and ran successfully on the Arduino MKR Zero hardware. |
| **Performance & Evidence Report** | [`evidence/README.md`](./evidence/README.md) | Hardware Serial UART captures (`*.txt`) and full Question 3 & Question 4 analysis. |
| **GCC ARM_CM0 Port Files** | [`src/FreeRTOS/portable/GCC/ARM_CM0/`](./src/FreeRTOS/portable/GCC/ARM_CM0) | `port.c` and `portmacro.h` for ARM Cortex-M0+ (`ATSAMD21G18A`). |
