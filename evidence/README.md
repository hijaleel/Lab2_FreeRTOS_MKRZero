# Lab 2 Individual Investigation: FreeRTOS Task Performance & Scheduler Configurations

**Platform:** Arduino MKR Zero (Microchip ATSAMD21G18A — ARM Cortex-M0+ @ 48 MHz)  
**Toolchain:** GCC (`arm-none-eabi-gcc`) with pure CMSIS (`sam.h`) register-level GPIO & SERCOM UART  
**Shared Resource:** Digital Pin 1 (`PORT_PA23`) + Onboard LED (`PB08`) managed by FreeRTOS synchronization primitive  

---
## 1. Experimental Configurations Tested

| Configuration | Preemption (`configUSE_PREEMPTION`) | Sync Primitive | Task Priorities (`const int`) | Observed LED (`PA23`) Behavior | Observed UART (`Task 3`) Behavior |
| :--- | :---: | :---: | :---: | :--- | :--- |
| **Baseline: Same Priority (Run to Completion)** | `0` (Cooperative) | Mutex | `T1 = 1`<br>`T2 = 1`<br>`T3 = 1` | **Blinks cleanly:** `500 ms` ON, `1000 ms` OFF | Prints heartbeat once per cycle (`T1 : T2 : T3 = 1 : 1 : 1`) |
| **Case (a): `Task 1 > Task 2 > Task 3` (Run to Completion)** | `0` (Cooperative) | Mutex | `T1 = 3`<br>`T2 = 2`<br>`T3 = 1` | **Stuck ON (3.3V, 100% duty cycle)** — never turns OFF | **Starved (`T2 = 0, T3 = 0`)** — Task 3 never runs |
| **Case (b): No Priority Inheritance** *(Literal `T1=3, T2=2, T3=1`)* | `1` (Preemptive) | Binary Semaphore | `T1 = 3`<br>`T2 = 2`<br>`T3 = 1` | **Stuck ON (3.3V)** — active-waiting `T1` starves `T2` & `T3` | **Starved (`T2 = 0, T3 = 0`)** |
| **Case (b-inv): No Priority Inheritance** *(Inversion Demo: `T2=3, T3=2, T1=1`)* | `1` (Preemptive) | Binary Semaphore | `T2 = 3`<br>`T3 = 2`<br>`T1 = 1` | Blinks, but **High-Prio `T2` is delayed** when `T3 (Prio 2)` preempts `T1 (EffPrio 1)` | Medium-Prio `T3` preempts Low-Prio `T1` mid-critical-section (**Priority Inversion**) |
| **Case (c): Priority Inheritance Enabled** *(Inversion Resolved: `T2=3, T3=2, T1=1`)* | `1` (Preemptive) | FreeRTOS Mutex | `T2 = 3`<br>`T3 = 2`<br>`T1 = 1` | **Exact `500 ms` ON / `1000 ms` OFF** — zero jitter | `T1` dynamically boosted **`EffPrio: 1 -> 3`**, blocking `T3` preemption until mutex release |

---

## 2. Real Hardware Serial UART Captures (Recorded on MKR Zero `COM9`)

### Baseline: All Three Tasks at the SAME Priority (`1, 1, 1`) — Run to Completion (`configUSE_PREEMPTION = 0`)
* **Evidence File:** [`case0_same_priority_rtc_uart.txt`](./case0_same_priority_rtc_uart.txt)
* **Binary:** [`../binaries/case0_same_priority_rtc.bin`](../binaries/case0_same_priority_rtc.bin)

```text
=== REAL HARDWARE UART CAPTURE (ATSAMD21G18A / MKR Zero on COM9) ===
Configuration: CASE 0 - All Tasks Same Priority (1, 1, 1), Run to Completion (configUSE_PREEMPTION = 0)
----------------------------------------------------------------------------------------------------
[t=24490 ms] Task1_LedOn  (EffPrio=1): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=17, T2=16, T3=17) | Pin1=ON
[t=25000 ms] Task1_LedOn  (EffPrio=1): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=17, T2=16, T3=17) | Pin1=ON
[t=25011 ms] Task2_LedOff (EffPrio=1): GPIO Pin 1 -> OFF (task-delay 1000ms) | Counts(T1=17, T2=17, T3=17) | Pin1=OFF
[t=25021 ms] Task3_Serial (EffPrio=1): UART Status Heartbeat (task-delay 1000ms) | Counts(T1=17, T2=17, T3=18) | Pin1=OFF
[t=26021 ms] Task1_LedOn  (EffPrio=1): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=18, T2=17, T3=18) | Pin1=ON
[t=26531 ms] Task1_LedOn  (EffPrio=1): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=18, T2=17, T3=18) | Pin1=ON
[t=26542 ms] Task2_LedOff (EffPrio=1): GPIO Pin 1 -> OFF (task-delay 1000ms) | Counts(T1=18, T2=18, T3=18) | Pin1=OFF
[t=26552 ms] Task3_Serial (EffPrio=1): UART Status Heartbeat (task-delay 1000ms) | Counts(T1=18, T2=18, T3=19) | Pin1=OFF
```

---

### Case (a): Task 1 Highest (`3`), Task 2 (`2`), Task 3 (`1`) — Run to Completion (`configUSE_PREEMPTION = 0`)
* **Evidence File:** [`caseA_task1_highest_rtc_uart.txt`](./caseA_task1_highest_rtc_uart.txt)
* **Binary:** [`../binaries/caseA_task1_highest_rtc.bin`](../binaries/caseA_task1_highest_rtc.bin)

```text
=== REAL HARDWARE UART CAPTURE (ATSAMD21G18A / MKR Zero on COM9) ===
Configuration: CASE (a) - Task 1 Highest (3), Task 2 (2), Task 3 (1), Run to Completion (configUSE_PREEMPTION = 0)
----------------------------------------------------------------------------------------------------
[t=40607 ms] Task1_LedOn  (EffPrio=3): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=78, T2=0, T3=0) | Pin1=ON
[t=40618 ms] Task1_LedOn  (EffPrio=3): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=79, T2=0, T3=0) | Pin1=ON
[t=41128 ms] Task1_LedOn  (EffPrio=3): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=79, T2=0, T3=0) | Pin1=ON
[t=41139 ms] Task1_LedOn  (EffPrio=3): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=80, T2=0, T3=0) | Pin1=ON
[t=41649 ms] Task1_LedOn  (EffPrio=3): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=80, T2=0, T3=0) | Pin1=ON
[t=41660 ms] Task1_LedOn  (EffPrio=3): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=81, T2=0, T3=0) | Pin1=ON
```
> **Observation:** `Counts(T1=81, T2=0, T3=0)` and `Pin1=ON` permanently! Because `Task1_LedOn` uses an **active spin-wait** and only calls `taskYIELD()` (never entering the `Blocked` state via `vTaskDelay()`), it remains Ready in `pxReadyTasksLists[3]`. In FreeRTOS, `taskYIELD()` only yields to tasks of **equal or higher priority (`>= 3`)**, so `Task2_LedOff` (`Prio 2`) and `Task3_Serial` (`Prio 1`) suffer **100% CPU starvation**.

---

### Case (b): No Priority Inheritance (`configUSE_PREEMPTION = 1`, Binary Semaphore)
* **Evidence File:** [`caseB_no_priority_inheritance_uart.txt`](./caseB_no_priority_inheritance_uart.txt)
* **Binary:** [`../binaries/caseB_no_priority_inheritance.bin`](../binaries/caseB_no_priority_inheritance.bin)

```text
=== REAL HARDWARE UART CAPTURE (ATSAMD21G18A / MKR Zero on COM9) ===
Configuration: CASE (b) - No Priority Inheritance (Preemptive=1, Binary Semaphore)
----------------------------------------------------------------------------------------------------
[t=100535 ms] Task1_LedOn  (EffPrio=1): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=66, T2=66, T3=100) | Pin1=ON
[t=101009 ms] Task3_Serial (EffPrio=2): UART Status Heartbeat (task-delay 1000ms) | Counts(T1=66, T2=66, T3=101) | Pin1=ON
[t=101045 ms] Task1_LedOn  (EffPrio=1): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=66, T2=66, T3=101) | Pin1=ON
[t=101056 ms] Task2_LedOff (EffPrio=3): GPIO Pin 1 -> OFF (task-delay 1000ms) | Counts(T1=66, T2=67, T3=101) | Pin1=OFF
```
> **Observation (Priority Inversion):** While `Task1_LedOn` (`EffPrio=1`) holds the binary semaphore during its 500 ms active wait (`t=100535..101045 ms`) and high-priority `Task2_LedOff` (`EffPrio=3`) is blocked waiting for it, `Task1_LedOn` stays at `EffPrio=1`. At `t=101009 ms`, medium-priority `Task3_Serial` (`EffPrio=2`) **preempts `Task1_LedOn`** mid-critical-section, delaying the release of the lock and forcing high-priority `Task2_LedOff` (`EffPrio=3`) to wait for a lower-priority task (`Task3_Serial`)!

---

### Case (c): Priority Inheritance Enabled (`configUSE_PREEMPTION = 1`, FreeRTOS Mutex)
* **Evidence File:** [`caseC_priority_inheritance_uart.txt`](./caseC_priority_inheritance_uart.txt)
* **Binary:** [`../binaries/caseC_priority_inheritance.bin`](../binaries/caseC_priority_inheritance.bin)

```text
=== REAL HARDWARE UART CAPTURE (ATSAMD21G18A / MKR Zero on COM9) ===
Configuration: CASE (c) - Priority Inheritance Enabled (Preemptive=1, FreeRTOS Mutex)
----------------------------------------------------------------------------------------------------
[t=36207 ms] Task1_LedOn  (EffPrio=1): GPIO Pin 1 -> ON  (active wait 500ms) | Counts(T1=24, T2=24, T3=36) | Pin1=ON
[t=36717 ms] Task1_LedOn  (EffPrio=3): 500ms active wait done -> Give mutex & taskYIELD() | Counts(T1=24, T2=24, T3=36) | Pin1=ON
[t=36728 ms] Task2_LedOff (EffPrio=3): GPIO Pin 1 -> OFF (task-delay 1000ms) | Counts(T1=24, T2=25, T3=36) | Pin1=OFF
[t=36738 ms] Task3_Serial (EffPrio=2): UART Status Heartbeat (task-delay 1000ms) | Counts(T1=24, T2=25, T3=37) | Pin1=OFF
```
> **Observation (Priority Inheritance Resolved):** `Task1_LedOn` enters the critical section at `t=36207 ms` with **`EffPrio=1`**. As soon as high-priority `Task2_LedOff` (`EffPrio=3`) blocks on `xLedMutex`, FreeRTOS **dynamically elevates `Task1_LedOn` to `EffPrio=3`** (`Task1_LedOn (EffPrio=3): 500ms active wait done`). Because `Task1_LedOn` is running at inherited Priority 3, medium-priority `Task3_Serial` (`EffPrio=2`) is prevented from preempting it, and `Task2_LedOff` (`EffPrio=3`) immediately acquires the mutex at `t=36728 ms` before `Task3_Serial` runs at `t=36738 ms`.

---

## 3. Conclusion: Which Scheduler Configuration Gave the "Better" Task Performance?

1. **Comparing Equal Priorities (1, 1, 1 — Run to Completion) vs. Literal `Task 1 (3) > Task 2 (2) > Task 3 (1)`:**
   * **All Three Tasks at the SAME Priority (Run to Completion)** gave vastly better performance.
   * **Why:** `Task 1` (`vTaskLedOn`) uses an **active busy-wait** (`500 ms`) followed by `taskYIELD()`, rather than a blocking call like `vTaskDelay()`. In FreeRTOS, `taskYIELD()` keeps the calling task in the `Ready` state and only yields to tasks of **equal or higher priority**. When `Task 1` is assigned the highest priority (`3`), `pxReadyTasksLists[3]` is never empty, so `taskYIELD()` immediately re-schedules `Task 1` every time, causing **complete CPU starvation** of `Task 2` (`T2 = 0`, LED never turns OFF) and `Task 3` (`T3 = 0`, no UART heartbeats). When all three tasks share the **same priority (`1`)**, `taskYIELD()` advances round-robin through `pxReadyTasksLists[1]`, allowing `Task 2` and `Task 3` to execute on schedule.

2. **Comparing Priority Inheritance Enabled (Case c) vs. No Priority Inheritance (Case b) under Preemptive Scheduling:**
   * **Priority Inheritance Enabled (`Case c`, using `xSemaphoreCreateMutex()`)** gave the best real-time determinism and responsiveness.
   * **Why:** When a low-priority active-waiting task (`EffPrio=1`) holds the shared GPIO mutex and a high-priority task (`EffPrio=3`) blocks waiting for that mutex, **Priority Inheritance** temporarily raises the mutex holder's effective priority from `1` to `3`. This prevents medium-priority tasks (`Task3_Serial`, `EffPrio=2`) from preempting the critical section (**preventing unbounded Priority Inversion**), ensuring the high-priority task acquires the GPIO pin immediately upon mutex release.
