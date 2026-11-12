/** @file isr_atomic_svc.c @brief ISR-03：SVC 资源交接窗口，由 GDB 注入更高优先级 IRQ。 */
#define ATOMIC_TICK 0
#define ATOMIC_CASE "ISR-03"
#include "isr_atomic_fixture.h"
