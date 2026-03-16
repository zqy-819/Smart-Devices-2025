/************************************************************
 *  Combined DHT11 + SGP30 + LED air-quality indicator
 *  ATmega328PB + UART + TWI0 (I2C)
 *
 *  DHT11  -> PD2
 *  LED(G,Y,R) -> PD3, PD4, PD5
 *  SGP30  -> PC4/PC5 (I2C)
 ************************************************************/
#ifndef F_CPU
#define F_CPU 16000000UL   // Xplained Mini ? 16 MHz
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <math.h>
#include "uart.h"
#include <stdio.h>

#include "code/ST7735.h"
#include "code/LCD_GFX.h"

/******************** LED DEFINITIONS ********************/
#define LED_DDR   DDRD
#define LED_PORT  PORTD

#define LED_GREEN PD3
#define LED_YELLOW PD4
#define LED_RED   PD5

#define LED_ALL_OFF()   (LED_PORT &= ~((1<<LED_GREEN)|(1<<LED_YELLOW)|(1<<LED_RED)))
#define LED_GREEN_ON()  do{LED_ALL_OFF(); LED_PORT |= (1<<LED_GREEN);}while(0)
#define LED_YELLOW_ON() do{LED_ALL_OFF(); LED_PORT |= (1<<LED_YELLOW);}while(0)
#define LED_RED_ON()    do{LED_ALL_OFF(); LED_PORT |= (1<<LED_RED);}while(0)

/******************** DHT11 DEFINITIONS ********************/
#define DHT_DDR   DDRD
#define DHT_PORT  PORTD
#define DHT_PINR  PIND
#define DHT_BIT   PD2

#define DHT_OUTPUT()   (DHT_DDR |=  (1 << DHT_BIT))
#define DHT_INPUT()    (DHT_DDR &= ~(1 << DHT_BIT))
#define DHT_HIGH()     (DHT_PORT |=  (1 << DHT_BIT))
#define DHT_LOW()      (DHT_PORT &= ~(1 << DHT_BIT))
#define DHT_READ()     ((DHT_PINR >> DHT_BIT) & 1)


#define LCD_BG      BLACK
#define LCD_FG      WHITE

// ???????
#define TEMP_LABEL_X   4
#define TEMP_LABEL_Y   6
#define TEMP_ICON_X    10
#define TEMP_ICON_Y    20
#define TEMP_VALUE_X   35
#define TEMP_VALUE_Y   10

// ???????
#define HUM_LABEL_X    90
#define HUM_LABEL_Y    6
#define HUM_ICON_X     96
#define HUM_ICON_Y     20
#define HUM_VALUE_X    120
#define HUM_VALUE_Y    10

// ???CO2 ??
#define CO2_LABEL_X    4
#define CO2_LABEL_Y    100
#define CO2_VALUE_X    30
#define CO2_VALUE_Y    100

#define CO2_BAR_X      4      // ?? X
#define CO2_BAR_Y      110    // ??????? 110 ????
#define CO2_BAR_W     60     // ???? 80????? TVOC ??
#define CO2_BAR_H      6   

// ???TVOC ??
#define TVOC_LABEL_X   85
#define TVOC_LABEL_Y   100
#define TVOC_VALUE_X   120
#define TVOC_VALUE_Y   100

#define TVOC_BAR_X     85
#define TVOC_BAR_Y     110
#define TVOC_BAR_W     60
#define TVOC_BAR_H     6

void buzzer_init(void)
{
    DDRC |= (1 << PC3);       // PC3 output
    PORTC &= ~(1 << PC3);     // start LOW
}

/******************** TIMER1 (0.5us) ********************/
void timer1_init_us(void)
{
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;
    TCCR1B |= (1 << CS11);  // clk/8 = 2MHz = 0.5 us/tick
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

uint8_t dht_read(uint8_t *hum, uint8_t *temp)
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

    for (uint8_t i=0;i<40;i++) {
        uint8_t b = dht_read_bit();
        if (b == 0xFF) return 4;
        data[i/8] <<= 1;
        data[i/8] |= b;
    }

    if ((uint8_t)(data[0]+data[1]+data[2]+data[3]) != data[4])
        return 5;

    *hum  = data[0];
    *temp = data[2];
    return 0;
}

/******************** TWI0 (SGP30) ********************/
#define SGP30_ADDR 0x58

void twi0_init(void)
{
    TWSR0 = 0x00;
    TWBR0 = 32;    // ~100kHz
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
    twi0_write((SGP30_ADDR<<1)|0);
    twi0_write(cmd>>8);
    twi0_write(cmd&0xFF);
    twi0_stop();
}

uint16_t sgp30_read_word(uint8_t last)
{
    uint8_t msb = twi0_read_ack();
    uint8_t lsb = twi0_read_ack();
    uint8_t crc = last ? twi0_read_nack() : twi0_read_ack();

    if (crc != sgp_crc(msb, lsb)) return 0xFFFF;

    return (msb<<8) | lsb;
}

void sgp30_init(void)
{
    _delay_ms(500);
    sgp30_write_cmd(0x2003);
    _delay_ms(10);
}

uint8_t sgp30_measure_iaq(uint16_t *co2, uint16_t *tvoc)
{
    sgp30_write_cmd(0x2008);
    _delay_ms(10);

    twi0_start();
    twi0_write((SGP30_ADDR<<1)|1);

    *co2 = sgp30_read_word(0);
    *tvoc = sgp30_read_word(1);

    twi0_stop();
    return (*co2 != 0xFFFF);
}

/******************** HUMIDITY COMPENSATION ********************/
float calc_abs_humidity(float T, float RH)
{
    float term = RH/100.0f * 6.112f * expf((17.62f*T)/(243.12f+T));
    return 216.7f * term / (273.15f + T);
}

void sgp30_set_absolute_humidity_raw(uint16_t ah)
{
    if (ah == 0) ah = 1;

    uint8_t msb = ah >> 8;
    uint8_t lsb = ah & 0xFF;
    uint8_t crc = sgp_crc(msb, lsb);

    twi0_start();
    twi0_write((SGP30_ADDR<<1)|0);
    twi0_write(0x20);
    twi0_write(0x61);
    twi0_write(msb);
    twi0_write(lsb);
    twi0_write(crc);
    twi0_stop();
}

void sgp30_update_humidity_from_dht(uint8_t t, uint8_t h)
{
    float dv = calc_abs_humidity(t, h);
    if (dv < 0) dv = 0;
    if (dv > 255) dv = 255;

    uint16_t fixed = (uint16_t)(dv * 256 + 0.5f);
    sgp30_set_absolute_humidity_raw(fixed);
}

/******************** AIR QUALITY LED LOGIC ********************/
void update_air_quality_led(uint16_t co2, uint16_t tvoc)
{
    if (co2 <= 600 && tvoc < 200)
        LED_GREEN_ON();

    else if (co2 <= 800 || tvoc < 400)
    {
        LED_YELLOW_ON();
        for(uint16_t i=0; i<500; i++)
        {     // slightly longer beep
        PORTC ^= (1 << PC3);
        _delay_us(125);                 // ~2kHz lower tone
        }
    }
    else
    {
        LED_RED_ON();
         for(uint16_t i=0; i<3000; i++)
         {
        PORTC ^= (1<<PC3);
        _delay_us(125);  // 4kHz
         }
    }
}

void lcd_draw_static_ui(void)
{
    // ?????
    LCD_setScreen(LCD_BG);

    /******** ????? ********/
    LCD_drawString(TEMP_LABEL_X, TEMP_LABEL_Y, "T:", LCD_FG, LCD_BG);

    // ???????? + ???
    lcd_drawRect(TEMP_ICON_X + 2, TEMP_ICON_Y, 11, 60, LCD_FG);
    LCD_drawCircle(TEMP_ICON_X + 7, TEMP_ICON_Y + 60, 8, LCD_FG);
    
    int16_t cx = TEMP_ICON_X + 8;
    int16_t cy = TEMP_ICON_Y + 60;
    int16_t r  = 8;
    for (int16_t y = -r; y <= r; y++)
    {
        int16_t dx = (int16_t)sqrt(r * r - y * y);
        lcd_fillRect(cx - dx, cy + y, (dx * 2) - 1, 1, RED);
    }

    /******** ????? ********/
    LCD_drawString(HUM_LABEL_X, HUM_LABEL_Y, "H:", LCD_FG, LCD_BG);

    // ??????? + ???? + ???
    uint8_t topX = HUM_ICON_X + 3;
    uint8_t topY = HUM_ICON_Y;
    /******** ????????????????? ********/
    LCD_drawString(HUM_LABEL_X, HUM_LABEL_Y, "H:", LCD_FG, LCD_BG);

    // ???????? + ???????????
    lcd_drawRect(HUM_ICON_X + 3, HUM_ICON_Y, 11, 60, LCD_FG);
    LCD_drawCircle(HUM_ICON_X + 8, HUM_ICON_Y + 60, 8, LCD_FG);
    
    int16_t hx = HUM_ICON_X + 10;     
    int16_t hy = HUM_ICON_Y + 60;     
    int16_t hr = 8;                   

    for (int16_t y = -hr; y <= hr; y++)
    {
        int16_t dx = (int16_t)sqrt(hr * hr - y * y);
        lcd_fillRect(hx - dx -1, hy + y, (dx * 2) - 1, 1, BLUE);
    }


    /******** ???CO2 ********/
    //LCD_drawString(CO2_LABEL_X, CO2_LABEL_Y, "CO2:", YELLOW, LCD_BG);

    /******** ???TVOC ********/
    LCD_drawString(TVOC_LABEL_X, TVOC_LABEL_Y, "TVOC:", CYAN, LCD_BG);
}

// ????????????????
void lcd_update_temp_bar(uint8_t t)
{
    uint8_t maxT        = 80;   // ?? 0~50°C
    if (t > maxT) t = maxT;

    uint8_t barMaxH     = 50;
    uint8_t barW        = 8;
    uint8_t barX        = TEMP_ICON_X + 3;
    uint8_t barBottomY  = TEMP_ICON_Y + 52;  // ????

    // ?????????????
    lcd_fillRect(barX, TEMP_ICON_Y + 2, barW, barMaxH, LCD_BG);

    if (t > 0)
    {
        uint8_t barH = (uint8_t)((uint16_t)t * barMaxH / maxT);
        uint8_t barY = barBottomY - barH;
        lcd_fillRect(barX, barY, barW, barH, RED);
    }
}

// ??????????????
void lcd_update_hum_bar(uint8_t h)
{
    if (h > 100) h = 100;

    uint8_t barMaxH     = 50;                 // ??????
    uint8_t barW        = 8;                  // ??????
    uint8_t barX        = HUM_ICON_X + 5;     // ????????
    uint8_t barBottomY  = HUM_ICON_Y + 52;    // ????? = 60 - 8

    // ?????
    lcd_fillRect(barX, HUM_ICON_Y + 2, barW, barMaxH, LCD_BG);

    if (h > 0)
    {
        uint8_t barH = (uint8_t)((uint16_t)h * barMaxH / 100);
        uint8_t barY = barBottomY - barH;
        lcd_fillRect(barX, barY, barW, barH, BLUE);
    }
}


// ?????????? + ???
void lcd_update_readings(uint8_t t, uint8_t h, uint16_t co2, uint16_t tvoc)
{
    char buf[16];

    /***** ??????? *****/
    lcd_fillRect(TEMP_VALUE_X, TEMP_VALUE_Y, 36, 10, LCD_BG);
    sprintf(buf, "%uC", t);
    LCD_drawString(TEMP_VALUE_X, TEMP_VALUE_Y, buf, LCD_FG, LCD_BG);

    /***** ??????? *****/
    lcd_fillRect(HUM_VALUE_X, HUM_VALUE_Y, 36, 10, LCD_BG);
    sprintf(buf, "%u%%", h);
    LCD_drawString(HUM_VALUE_X, HUM_VALUE_Y, buf, LCD_FG, LCD_BG);

    /***** ???CO2 ?? *****/
    lcd_fillRect(CO2_VALUE_X, CO2_VALUE_Y, 50, 10, LCD_BG);
    sprintf(buf, "%uppm", co2);
    uint16_t co2_color;
    if (co2 <= 600) {
    co2_color = GREEN;
    }
    else if (co2 <= 800) {
    co2_color = YELLOW;
    }
    else {
    co2_color = RED;
    }
    LCD_drawString(CO2_LABEL_X, CO2_LABEL_Y, "CO2:", co2_color, LCD_BG);
    LCD_drawString(CO2_VALUE_X, CO2_VALUE_Y, buf,     co2_color, LCD_BG);
    
    lcd_fillRect(CO2_BAR_X, CO2_BAR_Y, CO2_BAR_W, CO2_BAR_H, LCD_BG);
    uint16_t co2_clamped = co2;
    if (co2_clamped > 3000) co2_clamped = 3000;
    uint8_t co2_bar_len = (uint8_t)((uint32_t)co2_clamped * CO2_BAR_W / 3000);
    if (co2_bar_len > 0)
    {
        // ??? 1 ???????????????
        lcd_fillRect(CO2_BAR_X, CO2_BAR_Y + 1,
                     co2_bar_len, CO2_BAR_H - 2,
                     co2_color);  // ??????/LED ??
    }
    lcd_drawRect(CO2_BAR_X - 1, CO2_BAR_Y - 1, CO2_BAR_W + 2, CO2_BAR_H + 2, WHITE);

    /***** ???TVOC ?? *****/
    lcd_fillRect(TVOC_VALUE_X, TVOC_VALUE_Y, 60, 10, LCD_BG);
    sprintf(buf, "%uppb", tvoc);
    LCD_drawString(TVOC_VALUE_X, TVOC_VALUE_Y, buf, CYAN, LCD_BG);
    
    lcd_fillRect(TVOC_BAR_X, TVOC_BAR_Y, TVOC_BAR_W, TVOC_BAR_H, LCD_BG);
    uint16_t tvoc_clamped = tvoc;
    if (tvoc_clamped > 1000) tvoc_clamped = 1000;

    uint8_t tvoc_bar_len = (uint8_t)((uint32_t)tvoc_clamped * TVOC_BAR_W / 1000);

    if (tvoc_bar_len > 0)
    {
        // ????? 1 ?????????
        lcd_fillRect(TVOC_BAR_X,
                     TVOC_BAR_Y + 1,
                     tvoc_bar_len,
                     TVOC_BAR_H - 2,
                     CYAN);
    }
    lcd_drawRect(TVOC_BAR_X - 1, TVOC_BAR_Y - 1,TVOC_BAR_W + 2,TVOC_BAR_H + 2, WHITE);

    // ????/????????
    lcd_update_temp_bar(t);
    lcd_update_hum_bar(h);
}


/******************** MAIN ********************/
int main(void)
{
    uint8_t h, t;
    uint16_t co2, tvoc;

    uart_init();
    timer1_init_us();
    twi0_init();
    buzzer_init();

    LED_DDR |= (1<<LED_GREEN)|(1<<LED_YELLOW)|(1<<LED_RED);
    LED_ALL_OFF();

    uart_tx_str("System started...\r\n");

    sgp30_init();
    
    lcd_init();
    lcd_draw_static_ui();
    while (1)
    {
        /* ---- DHT11 ---- */
        uint8_t status = dht_read(&h, &t);

        if (status == 0) {
            uart_tx_str("[DHT11] H=");
            uart_tx_int(h);
            uart_tx_str("%  T=");
            uart_tx_int(t);
            uart_tx_str("C   ");

            // ? DHT ?????? SGP30 ?????
            sgp30_update_humidity_from_dht(t, h);
        } else {
            uart_tx_str("[DHT11 ERROR ");
            uart_tx_int(status);
            uart_tx_str("]   ");
        }

        /* ---- SGP30 ---- */
        if (sgp30_measure_iaq(&co2, &tvoc)) {
            uart_tx_str("[SGP30] CO2=");
            uart_tx_int(co2);
            uart_tx_str("ppm  TVOC=");
            uart_tx_int(tvoc);
            uart_tx_str("ppb   ");

            update_air_quality_led(co2, tvoc);

            // DHT ????LCD ??????
            if (status == 0) {
                lcd_update_readings(t, h, co2, tvoc);
            }
        } else {
            uart_tx_str("[SGP30 CRC ERROR]");

            // ???? SGP30 ?????? T/H????????
            if (status == 0) {
                lcd_update_readings(t, h, 0, 0);
            }
        }

        uart_tx_str("\r\n");
        _delay_ms(1000);
    }
}

