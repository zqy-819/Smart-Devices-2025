#include "uart.h"

void uart_init(void){
    UBRR0H = 0;
    UBRR0L = 103;                     // 9600 baud @16MHz
    UCSR0B = (1<<TXEN0);
    UCSR0C = (1<<UCSZ01)|(1<<UCSZ00);  // 8N1
}

void uart_tx_char(char c){
    while(!(UCSR0A & (1<<UDRE0)));
    UDR0 = c;
}
void uart_tx_float(float val) {
    char buf[16];
    dtostrf(val, 6, 2, buf);      // 宽度6，小数点后2位
    uart_tx_str(buf);
}

void uart_tx_str(const char *s){
    while(*s) uart_tx_char(*s++);
}

void uart_tx_int(unsigned int v){
    char buf[16]; int i=0;
    if(v==0){ uart_tx_char('0'); return;}
    while(v>0){ buf[i++] = '0' + (v%10); v /= 10; }
    while(i--) uart_tx_char(buf[i]);
}