/**
 ******************************************************************************
 * @file    syscalls.c
 * @brief   newlib 最小系统调用桩：让 printf 输出到 UART0
 *
 * newlib-nano 的 printf 最终调用 _write；其余函数提供最小实现，避免链接时缺符号。
 * 本工程不使用文件系统，也不读取输入，所以这些函数都只返回固定值。
 ******************************************************************************
 */
#include <errno.h>
#include <sys/stat.h>
#include "uart.h"

/**
 * @brief  printf 底层写函数：把数据送到 UART0
 * @param  fd  文件描述符（忽略，stdout/stderr 都走 UART0）
 * @param  buf 待写数据
 * @param  len 数据字节数
 * @return 实际写出的字节数（总是等于 len）
 * @note   遇到 '\n' 先补发 '\r'，这样终端和日志里换行都是 CRLF。
 */
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++)
    {
        if (buf[i] == '\n')
            uart_putc('\r');
        uart_putc(buf[i]);
    }
    return len;
}

/** @brief 读操作桩：没有输入设备，总是返回 0（EOF） */
int _read(int fd, char *buf, int len)
{
    (void)fd; (void)buf; (void)len;
    return 0;
}

/** @brief 关闭文件桩：不支持，返回 -1 */
int _close(int fd)
{
    (void)fd;
    return -1;
}

/** @brief 文件状态桩：声明为字符设备，使 stdio 按行缓冲/直写处理 */
int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

/** @brief 终端判断桩：认为所有描述符都是终端 */
int _isatty(int fd)
{
    (void)fd;
    return 1;
}

/** @brief 文件定位桩：字符设备不支持定位，返回 0 */
int _lseek(int fd, int off, int whence)
{
    (void)fd; (void)off; (void)whence;
    return 0;
}
