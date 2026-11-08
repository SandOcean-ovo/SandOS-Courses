/** @file neg_assert.c @brief NEG-08：内核断言死循环不能成为通过结果。 */
#include "os_core.h"
#include "test_support.h"
int main(void)
{
    test_begin("NEG-08");
    List_Init(NULL);
    test_finish();
}
