/** @file neg_exit_no_pass.c @brief NEG-07：只正常退出、不报告 PASS，必须失败。 */
#include "test_support.h"
int main(void)
{
    test_begin("NEG-07");
    test_exit(0);
}
