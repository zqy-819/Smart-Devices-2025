#include <avr/io.h>
#include <util/delay.h>
#include "uart.h"      // ⬅️ 加这个

#define DHT_DDR   DDRC
#define DHT_PORT  PORTC
#define DHT_PINR  PINC
#define DHT_BIT   PD2
#define DHT_OUTPUT()   (DHT_DDR |=  (1 << DHT_BIT))
#define DHT_INPUT()    (DHT_DDR &= ~(1 << DHT_BIT))

#define DHT_HIGH()     (DHT_PORT |=  (1 << DHT_BIT))
#define DHT_LOW()      (DHT_PORT &= ~(1 << DHT_BIT))

#define DHT_READ()     ((DHT_PINR >> DHT_BIT) & 1)

void timer1_init_us(void)
{
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;
    // CS11 = clk/8   (16 MHz / 8 = 2 MHz → 0.5µs per tick)
    TCCR1B |= (1 << CS11);
}

uint8_t wait_for_level(uint8_t level, uint16_t timeout_us)
{
    TCNT1 = 0;
    while (DHT_READ() != level) {
        if (TCNT1 >= timeout_us * 2) return 1; // 0.5us/tick → 100us = 200 ticks
    }
    return 0;
}

uint8_t dht_read_bit(void)
{
    if (wait_for_level(0, 100)) return 0xFF; 
    if (wait_for_level(1, 100)) return 0xFF;

    TCNT1 = 0;
    if (wait_for_level(0, 100)) return 0xFF;

    uint16_t high_ticks = TCNT1; // 0.5us /tick

    return (high_ticks > 80) ? 1 : 0;  // >40us = 1
}

uint8_t dht_read(uint8_t *humidity, uint8_t *temperature)
{
    uint8_t data[5] = {0};

    DHT_OUTPUT();
    DHT_LOW();
    _delay_ms(20);
    DHT_HIGH();
    _delay_us(30);
    DHT_INPUT();
    DHT_HIGH();

    if (wait_for_level(0, 100)) return 1;
    if (wait_for_level(1, 100)) return 2;
    if (wait_for_level(0, 100)) return 3;

    for (uint8_t i = 0; i < 40; i++)
    {
        uint8_t b = dht_read_bit();
        if (b == 0xFF) return 4;
        data[i/8] <<= 1;
        data[i/8] |= b;
    }

    if ((uint8_t)(data[0]+data[1]+data[2]+data[3]) != data[4])
        return 5;

    *humidity    = data[0];
    *temperature = data[2];

    return 0;
}


int main()
{
    uint8_t h, t;

    uart_init();        // ⬅️ 加 UART 初始化
    timer1_init_us();

    uart_tx_str("DHT11 start...\r\n"); // ⬅️ 欢迎语

    while (1)
    {
        uint8_t status = dht_read(&h, &t);

        if (status == 0) {
            uart_tx_str("Humidity: ");
            uart_tx_int(h);
            uart_tx_str("%  Temp: ");
            uart_tx_int(t);
            uart_tx_str(" C\r\n");
        } 
        else {
            uart_tx_str("DHT11 read error: ");
            uart_tx_int(status);
            uart_tx_str("\r\n");
        }

        _delay_ms(500);
    }
}