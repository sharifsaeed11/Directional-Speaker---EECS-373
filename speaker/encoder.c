#include "encoder.h"

// REQUIRES: uint8_t that corresponds to a valid port.
// EFFECTS:  Returns the corresponding XMC_GPIO_PORT_t.
XMC_GPIO_PORT_t * int2port(uint8_t port)
{
    switch (port) {
        case 0:  return XMC_GPIO_PORT0;
        case 1:  return XMC_GPIO_PORT1;
        case 2:  return XMC_GPIO_PORT2;
        case 3:  return XMC_GPIO_PORT3;
        case 4:  return XMC_GPIO_PORT4;
        case 5:  return XMC_GPIO_PORT5;
        case 6:  return XMC_GPIO_PORT6;
        case 7:  return XMC_GPIO_PORT7;
        case 8:  return XMC_GPIO_PORT8;
        case 9:  return XMC_GPIO_PORT9;
        case 14: return XMC_GPIO_PORT14;
        case 15: return XMC_GPIO_PORT15;
        default: return NULL;
    }
}

void encoder_init(Encoder * encoder, const uint8_t clk_port,
    const uint8_t clk_pin, const uint8_t dt_port,
    const uint8_t dt_pin, const uint8_t sw_port,
    const uint8_t sw_pin, const int MAX_POS,
    const int MIN_POS, const int INIT_POS) {

    // Assuming all pins already have pull-up resistors.
    XMC_GPIO_CONFIG_t cfg = {.mode = XMC_GPIO_MODE_INPUT_TRISTATE};

    XMC_GPIO_Init(int2port(clk_port), clk_pin, &cfg);
    XMC_GPIO_Init(int2port(dt_port),  dt_pin,  &cfg);
    XMC_GPIO_Init(int2port(sw_port),  sw_pin,  &cfg);

    encoder->clk_port = clk_port;
    encoder->clk_pin  = clk_pin;
    encoder->dt_port  = dt_port;
    encoder->dt_pin   = dt_pin;
    encoder->sw_port  = sw_port;
    encoder->sw_pin   = sw_pin;
    encoder->MAX_POS  = MAX_POS;
    encoder->MIN_POS  = MIN_POS;
    encoder->pos      = INIT_POS;

    // Read the starting state so the first read is not seen as a jump.
    uint8_t clk = XMC_GPIO_GetInput(int2port(clk_port), clk_pin);
    uint8_t dt  = XMC_GPIO_GetInput(int2port(dt_port),  dt_pin);
    encoder->state      = (clk << 1) | dt;
    encoder->prev_state = encoder->state;

}

int encoder_read(Encoder * encoder) {
    uint8_t clk = XMC_GPIO_GetInput(int2port(encoder->clk_port), encoder->clk_pin);
    uint8_t dt  = XMC_GPIO_GetInput(int2port(encoder->dt_port),  encoder->dt_pin);

    encoder->prev_state = encoder->state;
    encoder->state      = (clk << 1) | dt;

    switch (encoder->prev_state) {
        case 0b11:
            if (encoder->state == 0b01) encoder->pos++;   // clockwise
            if (encoder->state == 0b10) encoder->pos--;   // counterclockwise
            break;
        case 0b01:
            if (encoder->state == 0b00) encoder->pos++;   // clockwise
            if (encoder->state == 0b11) encoder->pos--;   // counterclockwise
            break;
        case 0b00:
            if (encoder->state == 0b10) encoder->pos++;   // clockwise
            if (encoder->state == 0b01) encoder->pos--;   // counterclockwise
            break;
        case 0b10:
            if (encoder->state == 0b11) encoder->pos++;   // clockwise
            if (encoder->state == 0b00) encoder->pos--;   // counterclockwise
            break;
    }

    if (encoder->pos > encoder->MAX_POS) encoder->pos = encoder->MAX_POS;
    if (encoder->pos < encoder->MIN_POS) encoder->pos = encoder->MIN_POS;

    return encoder->pos;
}

bool switch_read(Encoder * encoder) {
    encoder->sw_state = XMC_GPIO_GetInput(int2port(encoder->sw_port), encoder->sw_pin);
    return encoder->sw_state == 0; // Return TRUE if the state is 0. (Active Low)
}