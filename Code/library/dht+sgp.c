/************************************************************
 *  Combined DHT11 + SGP30 driver for ATmega328PB
 *  UART + Hardware I2C (TWI0)
 *
 *  DHT11 on PD2
 *  SGP30 on PC4/PC5  (TWI0)
 *  Now includes Absolute Humidity Compensation for SGP30
 ************************************************************/

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <math.h>       // ⭐ For expf()
#include "uart.h"

/******************** DHT11 DEFINITIONS ********************/

// --- Put DHT11 on PD2 ---
#define DHT_DDR   DDRD
#define DHT_PORT  PORTD
#define DHT_PINR  PIND
#define DHT_BIT   PD2

#define DHT_OUTPUT()   (DHT_DDR |=  (1 << DHT_BIT))
#define DHT_INPUT()    (DHT_DDR &= ~(1 << DHT_BIT))
#define DHT_HIGH()     (DHT_PORT |=  (1 << DHT_BIT))
#define DHT_LOW()      (DHT_PORT &= ~(1 << DHT_BIT))
#define DHT_READ()     ((DHT_PINR >> DHT_BIT) & 1)

/******************** TIMER1 (0.5us resolution) ********************/
void timer1_init_us(void)
{
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;
    TCCR1B |= (1 << CS11);  // clk/8 = 2MHz = 0.5us/tick
}

uint8_t wait_for_level(uint8_t level, uint16_t timeout_us)
{
    TCNT1 = 0;
    while (DHT_READ() != level) {
        if (TCNT1 >= timeout_us * 2) return 1;
    }
    return 0;
}

uint8_t dht_read_bit(void)
{
    if (wait_for_level(0, 100)) return 0xFF;
    if (wait_for_level(1, 100)) return 0xFF;

    TCNT1 = 0;
    if (wait_for_level(0, 100)) return 0xFF;

    uint16_t high_ticks = TCNT1;
    return (high_ticks > 80) ? 1 : 0;
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


/******************** TWI0 (SGP30) ********************/
#define SGP30_ADDR 0x58

void twi0_init(void)
{
    TWSR0 = 0;
    TWBR0 = 32;   // 100kHz
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

/************ SGP30 SUPPORT ************/
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
    twi0_write((SGP30_ADDR<<1) | 0);
    twi0_write(cmd >> 8);
    twi0_write(cmd & 0xFF);
    twi0_stop();
}

uint16_t sgp30_read_word(uint8_t last)
{
    uint8_t msb = twi0_read_ack();
    uint8_t lsb = twi0_read_ack();
    uint8_t crc = last ? twi0_read_nack() : twi0_read_ack();

    if (crc != sgp_crc(msb, lsb))
        return 0xFFFF;

    return (msb<<8) | lsb;
}

void sgp30_init(void)
{
    _delay_ms(500);
    sgp30_write_cmd(0x2003);  // iaq init
    _delay_ms(10);
}

uint8_t sgp30_measure_iaq(uint16_t *co2, uint16_t *tvoc)
{
    sgp30_write_cmd(0x2008);
    _delay_ms(10);

    twi0_start();
    twi0_write((SGP30_ADDR<<1)|1);

    *co2  = sgp30_read_word(0);
    *tvoc = sgp30_read_word(1);

    twi0_stop();

    return (*co2 != 0xFFFF);
}


/******************** ABSOLUTE HUMIDITY COMPENSATION ********************/

// 根据温度(°C) 和 RH(%) 计算绝对湿度 (g/m^3)
float calc_abs_humidity(float T, float RH)
{
    float term = RH/100.0f * 6.112f * expf((17.62f * T) / (243.12f + T));
    float dv   = 216.7f * term / (273.15f + T);
    return dv;   // g/m³
}

// 把 8.8 格式的绝对湿度写入 SGP30
void sgp30_set_absolute_humidity_raw(uint16_t ah_8_8)
{
    if (ah_8_8 == 0) ah_8_8 = 1;

    uint8_t msb = ah_8_8 >> 8;
    uint8_t lsb = ah_8_8 & 0xFF;
    uint8_t crc = sgp_crc(msb, lsb);

    twi0_start();
    twi0_write((SGP30_ADDR<<1) | 0);
    twi0_write(0x20);
    twi0_write(0x61);
    twi0_write(msb);
    twi0_write(lsb);
    twi0_write(crc);
    twi0_stop();
}

// 使用 DHT11 的温度和湿度更新 SGP30 的湿度补偿
void sgp30_update_humidity_from_dht(uint8_t t_degC, uint8_t rh_percent)
{
    float dv = calc_abs_humidity((float)t_degC, (float)rh_percent);

    if (dv < 0.0f) dv = 0.0f;
    if (dv > 255.0f) dv = 255.0f;

    uint16_t fixed_8_8 = (uint16_t)(dv * 256.0f + 0.5f);

    sgp30_set_absolute_humidity_raw(fixed_8_8);
}


/******************** MAIN ********************/
int main(void)
{
    uint8_t h, t;
    uint16_t co2, tvoc;

    uart_init();
    timer1_init_us();
    twi0_init();

    uart_tx_str("DHT11 + SGP30 (with humidity compensation) started...\r\n");

    sgp30_init();

    while(1)
    {
        /* ---- Read DHT11 ---- */
        uint8_t status = dht_read(&h, &t);

        if (status == 0) {
            uart_tx_str("[DHT11] H=");
            uart_tx_int(h);
            uart_tx_str("%  T=");
            uart_tx_int(t);
            uart_tx_str(" C   ");

            // ⭐ 更新 SGP30 湿度补偿
            sgp30_update_humidity_from_dht(t, h);
        } 
        else {
            uart_tx_str("[DHT11 ERROR ");
            uart_tx_int(status);
            uart_tx_str("]   ");
        }

        /* ---- Read SGP30 ---- */
        if (sgp30_measure_iaq(&co2, &tvoc))
        {
            uart_tx_str("[SGP30] CO2=");
            uart_tx_int(co2);
            uart_tx_str(" ppm  TVOC=");
            uart_tx_int(tvoc);
            uart_tx_str(" ppb");
        }
        else
        {
            uart_tx_str("[SGP30 CRC ERROR]");
        }

        uart_tx_str("\r\n");

        _delay_ms(1000);
    }
}