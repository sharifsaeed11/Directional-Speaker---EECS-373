#include "lcd_screen.h"
#include "encoder.h"

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

/* Used to demo the LCD screen working with the encoder. */
int main(void)
{
    // === GUI Config START ===
    cy_rslt_t result = cybsp_init();

    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(0);
        while (1) {}
    }
    // === GUI Config END ===

    Encoder encoder;
    encoder_init(&encoder, 3, 8, 3, 7, 3, 9, 3, 0, 0);

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
    lcd_print(">Song");

    for(int i = 1; i < 4; i++) {
        lcd_set_cursor(i, 1);
        lcd_print("Song");
    }

    int displayed_pos = encoder.pos;

    while (1) {
    
    encoder_read(&encoder);
    if (encoder.pos != displayed_pos) {
        lcd_set_cursor(displayed_pos, 0);
        lcd_print(" ");

        lcd_set_cursor(encoder.pos, 0);
        lcd_print(">");

        displayed_pos = encoder.pos;
    }

        // delay_ms(100); // Debouce Time
    }
}