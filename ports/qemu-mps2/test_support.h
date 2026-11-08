/** @file test_support.h @brief QEMU 测试协议；不作为通用内核 API。 */
#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H
#include <stdint.h>
void test_begin(const char *id);
void test_check(int condition, const char *name);
__attribute__((noreturn)) void test_finish(void);
/* 仅供运行器负例：直接退出，不自动输出 PASS/FAIL。 */
__attribute__((noreturn)) void test_exit(uint32_t status);
#endif
