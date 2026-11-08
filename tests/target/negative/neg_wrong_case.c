/** @file neg_wrong_case.c @brief NEG-10：编号不匹配，即使 PASS 并正常退出也必须失败。 */
#include "test_support.h"
int main(void)
{
    test_begin("WRONG-CASE");
    test_finish();
}
