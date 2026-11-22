#include "cybsp.h"
#include "cy_utils.h"
#include "xmc_gpio.h"
#include "xmc_uart.h"
#include <stdint.h>

#define TICKS_PER_SECOND 1000U

/* SysTick only tracks time. UART transmission happens in main(). */
void SysTick_Handler(void);

void delay_ms(uint32_t duration);

/* Send one raw byte to the LCD.
 * The conservative delay allows transmission and LCD processing.
 * This assumes the UART is configured for 9600 baud.
 */
void lcd_send_byte(uint8_t value);

/* Commands without a parameter */
void lcd_command(uint8_t command);

/* Commands with one parameter */
void lcd_command_parameter(uint8_t command, uint8_t parameter);

void lcd_print(const char *text);

/* Rows: 0–3. Columns: 0–19. */
void lcd_set_cursor(uint8_t row, uint8_t column);