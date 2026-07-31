#ifndef __BENCHMARK_H
#define __BENCHMARK_H

#include <stdint.h>

/* Set to 0 in a release build to keep the benchmark module inert. */
#ifndef OS_BENCHMARK_ENABLE
#define OS_BENCHMARK_ENABLE 1U
#endif

void Benchmark_RunAll(void);
uint8_t Benchmark_IsDWTAvailable(void);
void Benchmark_RuntimeCreateTasks(void);
void Benchmark_RuntimeRecordKernelTick(uint32_t cycles);

extern volatile uint8_t g_BenchmarkRuntimeActive;

#endif /* __BENCHMARK_H */
