#include "xmc_gpio.h"

typedef struct encoder {
    // GPIO Init Vairables
    uint8_t clk_port;
    uint8_t clk_pin;
    uint8_t dt_port;
    uint8_t dt_pin;
    uint8_t sw_port;
    uint8_t sw_pin;

    // Position Variables
    int MAX_POS;
    int MIN_POS;
    int INIT_POS;
    int pos;

    // State Variables
    uint8_t state;
    uint8_t prev_state;
    uint8_t sw_state;
    int8_t transition_count;
} Encoder;

// REQUIRES: An Empty Encoder, CLK Port & Pin, DT Port & Pin, SW Port & Pin,
//           Max Position, Min Position, Starting Position (Typically 0)
// MODIFIES: Encoder
// EFFECTS:  Initializes the provided encoder given the CLK, DT, and SW pin locations
//           and the position information for the desired application.
void encoder_init(Encoder * encoder,
    const uint8_t clk_port,
    const uint8_t clk_pin,
    const uint8_t dt_port,
    const uint8_t dt_pin,
    const uint8_t sw_port,
    const uint8_t sw_pin,
    const int MAX_POS,
    const int MIN_POS,
    const int INIT_POS);

// REQUIRES: Encoder
// MODIFIES: Encoder
// EFFECTS:  Reads the encoder state and modifies the current position and state.
int encoder_read(Encoder * encoder);

// REQUIRES: Encoder
// MODIFIES: Encoder
// EFFECTS:  Returns the current state of the switch as a bool. Updates the switch state.
bool switch_read(Encoder * encoder);