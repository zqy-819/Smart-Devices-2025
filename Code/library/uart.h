#ifndef UART_H
#define UART_H

#include <avr/io.h>

// 初始化串口
void uart_init(void);

// 发送单个字符
void uart_tx_char(char c);

// 发送字符串
void uart_tx_str(const char *s);

// 发送整数
void uart_tx_int(unsigned int v);

void uart_tx_float(float val);
#endif