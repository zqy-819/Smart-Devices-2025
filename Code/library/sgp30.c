/************************************************************
 *  SGP30 + ATmega328PB TWI0 Hardware I2C (PC4/PC5)
 *  Single-file version using your UART library
 *
 *  UART API from your uart.c:
 *     uart_init()
 *     uart_tx_str()
 *     uart_tx_char()
 *     uart_tx_int()
 *     uart_tx_float()
 ************************************************************/

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include "uart.h"

/******************** TWI0 (Hardware I2C) ********************/
void twi0_init(void)
{
    // SCL = F_CPU / (16 + 2*TWBR)
    TWSR0 = 0x00;    // prescaler = 1
    TWBR0 = 32;      // ~100kHz @ 16MHz
}

uint8_t twi0_start(void)
{
    TWCR0 = (1<<TWINT)|(1<<TWSTA)|(1<<TWEN);
    while(!(TWCR0 & (1<<TWINT)));
    return (TWSR0 & 0xF8);
}

uint8_t twi0_write(uint8_t data)
{
    TWDR0 = data;
    TWCR0 = (1<<TWINT)|(1<<TWEN);
    while(!(TWCR0 & (1<<TWINT)));
    return (TWSR0 & 0xF8);
}

uint8_t twi0_read_ack(void)
{
    TWCR0 = (1<<TWINT)|(1<<TWEN)|(1<<TWEA);
    while(!(TWCR0 & (1<<TWINT)));
    return TWDR0;
}

uint8_t twi0_read_nack(void)
{
    TWCR0 = (1<<TWINT)|(1<<TWEN);
    while(!(TWCR0 & (1<<TWINT)));
    return TWDR0;
}

void twi0_stop(void)
{
    TWCR0 = (1<<TWINT)|(1<<TWEN)|(1<<TWSTO);
}

/******************** SGP30 FUNCTIONS ********************/
#define SGP30_ADDR 0x58

// CRC per datasheet (poly 0x31, init 0xFF)
uint8_t sgp_crc(uint8_t msb, uint8_t lsb)
{
    uint8_t crc = 0xFF;
    uint8_t data[2] = {msb, lsb};

    for(uint8_t i=0;i<2;i++){
        crc ^= data[i];
        for(uint8_t b=0;b<8;b++)
            crc = (crc & 0x80) ? ((crc<<1) ^ 0x31) : (crc<<1);
    }
    return crc;
}

void sgp30_write_cmd(uint16_t cmd)
{
    twi0_start();
    twi0_write((SGP30_ADDR<<1) | 0);   // write
    twi0_write(cmd >> 8);
    twi0_write(cmd & 0xFF);
    twi0_stop();
}

uint16_t sgp30_read_word(uint8_t last)
{
    uint8_t msb = twi0_read_ack();
    uint8_t lsb = twi0_read_ack();
    uint8_t crc;

    if (last)
        crc = twi0_read_nack();   // 最后一个字，不再继续读
    else
        crc = twi0_read_ack();    // ACK CRC，继续下一个 word

    if(crc != sgp_crc(msb, lsb))
        return 0xFFFF;

    return (msb<<8) | lsb;
}

void sgp30_init(void)
{
    _delay_ms(500);              // sensor power-up time
    sgp30_write_cmd(0x2003);     // sgp30_iaq_init
    _delay_ms(10);
}

uint8_t sgp30_measure_iaq(uint16_t *co2, uint16_t *tvoc)
{
    sgp30_write_cmd(0x2008);     // sgp30_measure_iaq
    _delay_ms(10);               // measurement duration

    twi0_start();
    twi0_write((SGP30_ADDR<<1)|1);    // read

    *co2  = sgp30_read_word(0);  // 不是最后一个 word
    *tvoc = sgp30_read_word(1);  // 最后一个 word

    twi0_stop();

    return (*co2 != 0xFFFF);
}

/******************** MAIN ********************/
int main(void)
{
    uart_init();
    twi0_init();

    uart_tx_str("SGP30 started...\r\n");
    sgp30_init();

    while(1)
    {
        uint16_t co2, tvoc;

        if(sgp30_measure_iaq(&co2, &tvoc))
        {
            uart_tx_str("CO2eq=");
            uart_tx_int(co2);
            uart_tx_str(" ppm, TVOC=");
            uart_tx_int(tvoc);
            uart_tx_str(" ppb\r\n");
        }
        else
        {
            uart_tx_str("CRC error\r\n");
        }

        _delay_ms(1000);  // 1 Hz sampling
    }
}

