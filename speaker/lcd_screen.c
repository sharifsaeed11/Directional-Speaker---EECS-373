#include "lcd_screen.h"

static volatile uint32_t milliseconds = 0;

/* SysTick only tracks time. UART transmission happens in main(). */
void SysTick_Handler(void)
{
    milliseconds++;
}

void delay_ms(uint32_t duration)
{
    uint32_t start = milliseconds;

    while ((uint32_t)(milliseconds - start) < duration)
    {
    }
}

/* Send one raw byte to the LCD.
 * The conservative delay allows transmission and LCD processing.
 * This assumes the UART is configured for 9600 baud.
 */
void lcd_send_byte(uint8_t value)
{
    XMC_UART_CH_Transmit(CYBSP_DEBUG_UART_HW, value);
    // delay_ms(3);
}

/* Commands without a parameter */
void lcd_command(uint8_t command)
{
    lcd_send_byte(0xFE);
    lcd_send_byte(command);
}

/* Commands with one parameter */
void lcd_command_parameter(uint8_t command, uint8_t parameter)
{
    lcd_send_byte(0xFE);
    lcd_send_byte(command);
    lcd_send_byte(parameter);
}

void lcd_print(const char *text)
{
    while (*text != '\0')
    {
        lcd_send_byte((uint8_t)*text);
        text++;
    }
}

/* Rows: 0–3. Columns: 0–19. */
void lcd_set_cursor(uint8_t row, uint8_t column)
{
    static const uint8_t row_address[4] = {
        0x00, 0x40, 0x14, 0x54
    };

    if ((row < 4U) && (column < 20U))
    {
        lcd_command_parameter(
            0x45,
            (uint8_t)(row_address[row] + column)
        );
    }
}

int main(void)
{
    cy_rslt_t result = cybsp_init();

    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
        while (1) {}
    }

    XMC_UART_CH_Start(CYBSP_DEBUG_UART_HW);

    if (SysTick_Config(SystemCoreClock / TICKS_PER_SECOND) != 0U)
    {
        CY_ASSERT(0);
        while (1) {}
    }

    /* Allow the LCD's onboard controller to initialize. */
    delay_ms(150);

    lcd_command(0x41);                  // Display on
    lcd_command_parameter(0x52, 40);    // Contrast: 1–50
    lcd_command_parameter(0x53, 8);     // Brightness: 1–8
    lcd_command(0x51);                  // Clear screen

    lcd_set_cursor(0, 0);
    lcd_print("Hello from XMC4700");

    lcd_set_cursor(1, 0);
    lcd_print("Audio system ready");

    while (1)
    {
        XMC_GPIO_ToggleOutput(
            CYBSP_USER_LED_PORT,
            CYBSP_USER_LED_PIN
        );

        // delay_ms(1000);
    }
}
