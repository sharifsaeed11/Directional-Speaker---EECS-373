/******************************************************************************
* File Name:   sd_card.c
*
* Description: Minimal, polled SD card driver for the XMC4700 Relax Kit's
*              on-board microSD slot, built only on the XMCLib low-level
*              SDMMC driver (xmc_sdmmc.h). See sd_card.h for the API.
*
*              How it works, top to bottom:
*                1. sd_pins_init()   - hand the 6 SD pins to the SDMMC peripheral
*                2. sd_clock_init()  - give the SDMMC peripheral its 48 MHz clock
*                3. host setup       - reset, power, 375 kHz SD clock
*                4. identification   - CMD0, CMD8, ACMD41, CMD2, CMD3, CMD9, CMD7
*                5. speed up         - faster SD clock (optionally ACMD6, 4-bit bus)
*                6. read/write       - CMD17 (read block), CMD24 (write block)
*
*              Command numbers (CMDx/ACMDx) come from the SD Association's
*              "Physical Layer Simplified Specification", which is free online.
*
* EECS 373 example project.
******************************************************************************/

#include <string.h>
#include "sd_card.h"
#include "xmc_sdmmc.h"
#include "xmc_gpio.h"
#include "xmc_scu.h"

/*******************************************************************************
* Settings you may want to change
*******************************************************************************/

/* SDMMC peripheral input clock. 48 MHz is the maximum the SDMMC block allows. */
#define SDMMC_MODULE_CLK_HZ         (48000000UL)

/* SD clock during identification. The SD spec requires <= 400 kHz.
 * 48 MHz / 128 = 375 kHz. */
#define SD_INIT_CLK_DIV             XMC_SDMMC_CLK_DIV_128

/* SD clock for normal reads/writes. Default-speed cards allow up to 25 MHz.
 * 48 MHz / 4 = 12 MHz is a conservative choice. */
// #define SD_TRANSFER_CLK_DIV         XMC_SDMMC_CLK_DIV_4
#define SD_TRANSFER_CLK_DIV         XMC_SDMMC_CLK_DIV_2

/* Data bus width: 0 = 1-bit (DAT0 only), 1 = 4-bit (DAT0-DAT3).
 * Tested on a real XMC4700 Relax Kit: 1-bit mode was reliable, while 4-bit
 * mode failed about half the time with data CRC errors (the result changed
 * at random on every bus-width switch, at any clock speed). 1-bit at 12 MHz
 * is about 1.5 MB/s, plenty for data logging. Try 4-bit with your card if
 * you need more speed. */
// #define SD_USE_4BIT_BUS             (0)
#define SD_USE_4BIT_BUS             (1)

/* Software timeouts (milliseconds). The SDMMC hardware also has its own
 * command/data timeouts; these are a safety net so nothing hangs forever. */
#define SD_CMD_TIMEOUT_MS           (100U)
#define SD_INIT_TIMEOUT_MS          (1000U)  /* ACMD41: card power-up can take up to 1 s */
#define SD_READ_TIMEOUT_MS          (250U)
#define SD_WRITE_TIMEOUT_MS         (1000U)  /* spec allows up to 500 ms busy for SDXC writes */
#define SD_READY_TIMEOUT_MS         (500U)

/*******************************************************************************
* Hardware description
*******************************************************************************/

#define SD_HOST                     XMC_SDMMC

/* The microSD slot on the XMC4700 Relax Kit (see kit schematic and the
 * "Port I/O Functions" table in the XMC4700 data sheet). The pin-to-signal
 * mapping is fixed in silicon: setting a pin to hardware control
 * "PERIPHERAL1" (HW0/HW1 select) connects it to the SDMMC module.
 *
 * NOTE: the BSP's cycfg_pins.h labels P4.1 as DAT0 and P4.0 as DAT3, but the
 * XMCLib pin map (xmc4_gpio_map.h) says P4.0 = SDMMC DATA0 and P4.1 = DATA3.
 * Every pin below gets the same setup, so the label mix-up has no effect. */
typedef struct
{
    XMC_GPIO_PORT_t *port;
    uint8_t          pin;
    XMC_GPIO_HWCTRL_t hwctrl;
} sd_pin_t;

static const sd_pin_t sd_pins[] =
{
    { XMC_GPIO_PORT3, 5U, P3_5_HWCTRL_SDMMC_CMD_OUT   }, /* CMD  */
    { XMC_GPIO_PORT3, 6U, P3_6_HWCTRL_SDMMC_CLK_OUT   }, /* CLK  */
    { XMC_GPIO_PORT4, 0U, P4_0_HWCTRL_SDMMC_DATA_OUT0 }, /* DAT0 */
    { XMC_GPIO_PORT1, 6U, P1_6_HWCTRL_SDMMC_DATA_OUT1 }, /* DAT1 */
    { XMC_GPIO_PORT1, 7U, P1_7_HWCTRL_SDMMC_DATA_OUT2 }, /* DAT2 */
    { XMC_GPIO_PORT4, 1U, P4_1_HWCTRL_SDMMC_DATA_OUT3 }, /* DAT3 */

};

/* Interrupt-status bits we want the host controller to report. We never
 * enable the actual interrupt signal; we just poll these status bits. */
#define SD_EVENTS_USED  ((uint32_t)XMC_SDMMC_CMD_COMPLETE      | \
                         (uint32_t)XMC_SDMMC_TX_COMPLETE       | \
                         (uint32_t)XMC_SDMMC_BUFFER_WRITE_READY| \
                         (uint32_t)XMC_SDMMC_BUFFER_READ_READY | \
                         (uint32_t)XMC_SDMMC_CMD_TIMEOUT_ERR   | \
                         (uint32_t)XMC_SDMMC_CMD_CRC_ERR       | \
                         (uint32_t)XMC_SDMMC_CMD_END_BIT_ERR   | \
                         (uint32_t)XMC_SDMMC_CMD_IND_ERR       | \
                         (uint32_t)XMC_SDMMC_DATA_TIMEOUT_ERR  | \
                         (uint32_t)XMC_SDMMC_DATA_CRC_ERR      | \
                         (uint32_t)XMC_SDMMC_DATA_END_BIT_ERR  | \
                         (uint32_t)XMC_SDMMC_CURRENT_LIMIT_ERR)

/*******************************************************************************
* SD protocol constants
*******************************************************************************/

/* Response formats (SD spec section 4.9) */
typedef enum
{
    RSP_NONE,   /* no response (CMD0) */
    RSP_R1,     /* 48-bit card status */
    RSP_R1B,    /* R1 + card holds DAT0 low while busy */
    RSP_R2,     /* 136-bit CID or CSD register */
    RSP_R3,     /* 48-bit OCR register, no CRC */
    RSP_R6,     /* 48-bit published RCA */
    RSP_R7      /* 48-bit interface condition (CMD8) */
} sd_rsp_t;

#define CMD0_GO_IDLE_STATE          (0U)
#define CMD2_ALL_SEND_CID           (2U)
#define CMD3_SEND_RELATIVE_ADDR     (3U)
#define ACMD6_SET_BUS_WIDTH         (6U)
#define CMD7_SELECT_CARD            (7U)
#define CMD8_SEND_IF_COND           (8U)
#define CMD9_SEND_CSD               (9U)
#define CMD13_SEND_STATUS           (13U)
#define CMD16_SET_BLOCKLEN          (16U)
#define CMD17_READ_SINGLE_BLOCK     (17U)
#define CMD24_WRITE_BLOCK           (24U)
#define ACMD41_SD_SEND_OP_COND      (41U)
#define CMD55_APP_CMD               (55U)

/* CMD8 argument: 2.7-3.6 V supply + check pattern 0xAA. Card echoes it back. */
#define CMD8_ARG                    (0x000001AAUL)

/* ACMD41 argument / OCR bits */
#define OCR_VOLTAGE_WINDOW          (0x00FF8000UL) /* 2.7 V - 3.6 V */
#define OCR_HCS_CCS                 (1UL << 30)    /* host supports / card is high capacity */
#define OCR_POWER_UP_DONE           (1UL << 31)    /* card finished power-up */

/* Card status (R1) bits */
#define R1_APP_CMD                  (1UL << 5)
#define R1_READY_FOR_DATA           (1UL << 8)
#define R1_CURRENT_STATE(s)         (((s) >> 9) & 0xFUL)
#define R1_STATE_TRAN               (4UL)
#define R1_ERROR_BITS               (0xFDF98008UL) /* all "error" bits in the card status */

/* ACMD6 argument for a 4-bit bus */
#define ACMD6_BUS_WIDTH_4           (2UL)

/*******************************************************************************
* Module state
*******************************************************************************/

static sd_card_info_t s_info;
static bool           s_initialized = false;

/*******************************************************************************
* Timing (DWT cycle counter, no interrupts needed)
*******************************************************************************/

static void sd_timer_init(void)
{
    SystemCoreClockUpdate();
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; /* enable DWT block */
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;      /* start cycle counter */
}

static uint32_t sd_now(void)
{
    return DWT->CYCCNT;
}

/* true once 'ms' milliseconds have passed since 'start' (handles wrap-around). */
static bool sd_elapsed(uint32_t start, uint32_t ms)
{
    return (sd_now() - start) >= (ms * (SystemCoreClock / 1000UL));
}

void sd_delay_ms(uint32_t ms)
{
    sd_timer_init();
    /* Wait 1 ms at a time so long delays cannot overflow the cycle math. */
    while (ms-- > 0U)
    {
        uint32_t start = sd_now();
        while (!sd_elapsed(start, 1U))
        {
        }
    }
}

/*******************************************************************************
* Step 1 + 2: pins and clocks
*******************************************************************************/

static void sd_pins_init(void)
{
    /* Inputs with pull-ups (SD lines idle high). Once hardware control is
     * selected the SDMMC module decides when each pin drives. */
    const XMC_GPIO_CONFIG_t cfg =
    {
        .mode            = XMC_GPIO_MODE_INPUT_PULL_UP,
        .output_level    = XMC_GPIO_OUTPUT_LEVEL_HIGH,
        .output_strength = XMC_GPIO_OUTPUT_STRENGTH_STRONG_MEDIUM_EDGE,
    };

    for (uint32_t i = 0U; i < (sizeof(sd_pins) / sizeof(sd_pins[0])); i++)
    {
        XMC_GPIO_Init(sd_pins[i].port, sd_pins[i].pin, &cfg);
        XMC_GPIO_SetOutputStrength(sd_pins[i].port, sd_pins[i].pin,
                                   XMC_GPIO_OUTPUT_STRENGTH_STRONG_MEDIUM_EDGE);
        XMC_GPIO_SetHardwareControl(sd_pins[i].port, sd_pins[i].pin, sd_pins[i].hwctrl);
    }
}

static void sd_clock_init(void)
{
    /* The SDMMC module clock is shared with USB ("fUSB/SDMMC"). The BSP does
     * not enable the USB PLL, so derive it from the main PLL instead:
     * fPLL = 288 MHz on this board, 288 / 6 = 48 MHz. */
    uint32_t f_pll = XMC_SCU_CLOCK_GetSystemPllClockFrequency();
    uint32_t div   = (f_pll + SDMMC_MODULE_CLK_HZ - 1UL) / SDMMC_MODULE_CLK_HZ; /* round up -> <= 48 MHz */

    XMC_SCU_CLOCK_SetUsbClockSource(XMC_SCU_CLOCK_USBCLKSRC_SYSPLL);
    XMC_SCU_CLOCK_SetUsbClockDivider(div);
    XMC_SCU_CLOCK_EnableClock(XMC_SCU_CLOCK_MMC);
}

/*******************************************************************************
* Low-level host controller helpers
*******************************************************************************/

static void sd_clear_events(void)
{
    /* Status bits are cleared by writing 1s. */
    SD_HOST->INT_STATUS_ERR  = 0xFFFFU;
    SD_HOST->INT_STATUS_NORM = 0xFFFFU;
}

/* Reset the command and/or data state machines after an error. */
static void sd_reset_lines(uint32_t which)
{
    XMC_SDMMC_SetSWReset(SD_HOST, which);
    uint32_t start = sd_now();
    while ((XMC_SDMMC_GetSWResetStatus(SD_HOST) & which) != 0U)
    {
        if (sd_elapsed(start, SD_CMD_TIMEOUT_MS))
        {
            break;
        }
    }
}

/* Poll until every bit in 'events' is set in INT_STATUS_NORM, any error bit is
 * set, or the timeout expires. The bits we waited for are cleared. */
static sd_result_t sd_wait_event(uint16_t events, uint32_t timeout_ms)
{
    uint32_t start = sd_now();
    for (;;)
    {
        uint16_t err = SD_HOST->INT_STATUS_ERR;
        if (err != 0U)
        {
            SD_HOST->INT_STATUS_ERR = err;
            if ((err & SDMMC_INT_STATUS_ERR_CMD_TIMEOUT_ERR_Msk) != 0U)
            {
                return SD_ERR_TIMEOUT;
            }
            if ((err & (SDMMC_INT_STATUS_ERR_CMD_CRC_ERR_Msk |
                        SDMMC_INT_STATUS_ERR_CMD_END_BIT_ERR_Msk |
                        SDMMC_INT_STATUS_ERR_CMD_IND_ERR_Msk)) != 0U)
            {
                return SD_ERR_CMD;
            }
            return SD_ERR_DATA;
        }

        if ((SD_HOST->INT_STATUS_NORM & events) == events)
        {
            SD_HOST->INT_STATUS_NORM = events;
            return SD_OK;
        }

        if (sd_elapsed(start, timeout_ms))
        {
            return SD_ERR_TIMEOUT;
        }
    }
}

/* Change the SD clock divider (SD Host Controller spec: stop SDCLK, change
 * divider, wait for internal clock stable, restart SDCLK). */
static sd_result_t sd_set_clock(XMC_SDMMC_SDCLK_FREQ_SEL_t div)
{
    XMC_SDMMC_SDClockDisable(SD_HOST);
    XMC_SDMMC_SDClockFreqSelect(SD_HOST, div);
    XMC_SDMMC_Start(SD_HOST); /* internal clock enable */

    uint32_t start = sd_now();
    while (!XMC_SDMMC_GetClockStability(SD_HOST))
    {
        if (sd_elapsed(start, SD_CMD_TIMEOUT_MS))
        {
            return SD_ERR_TIMEOUT;
        }
    }

    XMC_SDMMC_SDClockEnable(SD_HOST);
    return SD_OK;
}

/* Send one command and wait for its response.
 *   index     - command number (e.g. 17 for CMD17)
 *   arg       - 32-bit command argument
 *   rsp       - expected response format
 *   has_data  - true if a data block follows (CMD17/CMD24)
 *   resp      - where to store the response (1 word, or 4 words for R2); may be NULL */
static sd_result_t sd_send_cmd(uint8_t index, uint32_t arg, sd_rsp_t rsp, bool has_data, uint32_t *resp)
{
    XMC_SDMMC_COMMAND_t cmd;
    cmd.cmd             = 0U;
    cmd.cmd_index       = index;
    cmd.cmd_type        = XMC_SDMMC_COMMAND_TYPE_NORMAL;
    cmd.dat_present_sel = has_data ? 1U : 0U;

    switch (rsp)
    {
        case RSP_NONE:
            cmd.response_type_sel = XMC_SDMMC_COMMAND_RESPONSE_NONE;
            break;
        case RSP_R2:
            cmd.response_type_sel = XMC_SDMMC_COMMAND_RESPONSE_LONG;
            cmd.crc_check_en      = 1U;
            break;
        case RSP_R3:
            cmd.response_type_sel = XMC_SDMMC_COMMAND_RESPONSE_SHORT; /* OCR has no valid CRC/index */
            break;
        case RSP_R1B:
            cmd.response_type_sel = XMC_SDMMC_COMMAND_RESPONSE_SHORT_BUSY;
            cmd.crc_check_en      = 1U;
            cmd.index_check_en    = 1U;
            break;
        default: /* R1, R6, R7 */
            cmd.response_type_sel = XMC_SDMMC_COMMAND_RESPONSE_SHORT;
            cmd.crc_check_en      = 1U;
            cmd.index_check_en    = 1U;
            break;
    }

    /* Wait until the host is allowed to issue a command. Commands that use
     * the DAT lines (data or busy) must also wait for the DAT lines. */
    bool uses_dat = has_data || (rsp == RSP_R1B);
    uint32_t start = sd_now();
    while (XMC_SDMMC_IsCommandLineBusy(SD_HOST) || (uses_dat && XMC_SDMMC_IsDataLineBusy(SD_HOST)))
    {
        if (sd_elapsed(start, SD_CMD_TIMEOUT_MS))
        {
            return SD_ERR_TIMEOUT;
        }
    }

    sd_clear_events();

    /* Issue the command. Writing the COMMAND register starts it.
     *
     * IMPORTANT (found while testing on the real board): TRANSFER_MODE
     * (offset 0x0C) and COMMAND (offset 0x0E) must be written TOGETHER in a
     * single 32-bit write. XMCLib's XMC_SDMMC_SendCommand() writes only the
     * 16-bit COMMAND register; the XMC4700 then ignores the transfer
     * direction we set, and every block read turns into a write (the host
     * reports "write transfer active" and the read never completes). */
    SD_HOST->ARGUMENT1 = arg;
    *(volatile uint32_t *)&SD_HOST->TRANSFER_MODE =
        (uint32_t)SD_HOST->TRANSFER_MODE | ((uint32_t)cmd.cmd << 16);

    sd_result_t result = sd_wait_event((uint16_t)XMC_SDMMC_CMD_COMPLETE, SD_CMD_TIMEOUT_MS);
    if (result != SD_OK)
    {
        sd_reset_lines((uint32_t)XMC_SDMMC_SW_RST_CMD_LINE |
                       (uses_dat ? (uint32_t)XMC_SDMMC_SW_RST_DAT_LINE : 0U));
        return result;
    }

    if (resp != NULL)
    {
        if (rsp == RSP_R2)
        {
            XMC_SDMMC_RESPONSE_t r2;
            XMC_SDMMC_GetR2Response(SD_HOST, &r2);
            resp[0] = r2.response_0;
            resp[1] = r2.response_2;
            resp[2] = r2.response_4;
            resp[3] = r2.response_6;
        }
        else
        {
            resp[0] = XMC_SDMMC_GetCommandResponse(SD_HOST);
        }
    }

    /* R1b: wait for the card to release DAT0 (end of busy). */
    if (rsp == RSP_R1B)
    {
        start = sd_now();
        while (XMC_SDMMC_IsDataLineBusy(SD_HOST))
        {
            if (sd_elapsed(start, SD_WRITE_TIMEOUT_MS))
            {
                sd_reset_lines((uint32_t)XMC_SDMMC_SW_RST_CMD_LINE | (uint32_t)XMC_SDMMC_SW_RST_DAT_LINE);
                return SD_ERR_TIMEOUT;
            }
        }
        SD_HOST->INT_STATUS_NORM = (uint16_t)XMC_SDMMC_TX_COMPLETE;
    }

    return SD_OK;
}

/* Application-specific command: CMD55 followed by the ACMD. */
static sd_result_t sd_send_acmd(uint8_t index, uint32_t arg, sd_rsp_t rsp, uint32_t *resp)
{
    uint32_t status;
    sd_result_t result = sd_send_cmd(CMD55_APP_CMD, (uint32_t)s_info.rca << 16, RSP_R1, false, &status);
    if (result != SD_OK)
    {
        return result;
    }
    if ((status & R1_APP_CMD) == 0U)
    {
        return SD_ERR_CARD_STATUS;
    }
    return sd_send_cmd(index, arg, rsp, false, resp);
}

/* Ask the card for its status (CMD13) until it is in the "transfer" state and
 * ready for data, i.e. it has finished any previous write. */
static sd_result_t sd_wait_card_ready(void)
{
    uint32_t start = sd_now();
    for (;;)
    {
        uint32_t status;
        sd_result_t result = sd_send_cmd(CMD13_SEND_STATUS, (uint32_t)s_info.rca << 16, RSP_R1, false, &status);
        if (result != SD_OK)
        {
            return result;
        }
        if ((status & R1_ERROR_BITS) != 0U)
        {
            return SD_ERR_CARD_STATUS;
        }
        if (((status & R1_READY_FOR_DATA) != 0U) && (R1_CURRENT_STATE(status) == R1_STATE_TRAN))
        {
            return SD_OK;
        }
        if (sd_elapsed(start, SD_READY_TIMEOUT_MS))
        {
            return SD_ERR_TIMEOUT;
        }
    }
}

/*******************************************************************************
* CSD decoding (card capacity)
*******************************************************************************/

/* Extract CSD bits [msb:lsb]. The host strips the 8-bit CRC, so CSD bit N is
 * stored at bit (N - 8) of the four 32-bit response words. */
static uint32_t sd_csd_bits(const uint32_t csd[4], uint32_t msb, uint32_t lsb)
{
    uint32_t value = 0U;
    for (int32_t bit = (int32_t)msb; bit >= (int32_t)lsb; bit--)
    {
        uint32_t b = (uint32_t)bit - 8U;
        value = (value << 1) | ((csd[b / 32U] >> (b % 32U)) & 1U);
    }
    return value;
}

static uint32_t sd_csd_block_count(const uint32_t csd[4])
{
    uint32_t structure = sd_csd_bits(csd, 127U, 126U);

    if (structure == 1U)
    {
        /* CSD version 2.0 (SDHC/SDXC): capacity = (C_SIZE + 1) * 512 KiB */
        uint32_t c_size = sd_csd_bits(csd, 69U, 48U);
        return (c_size + 1U) * 1024U;
    }

    /* CSD version 1.0 (SDSC) */
    uint32_t c_size      = sd_csd_bits(csd, 73U, 62U);
    uint32_t c_size_mult = sd_csd_bits(csd, 49U, 47U);
    uint32_t read_bl_len = sd_csd_bits(csd, 83U, 80U);
    uint32_t bytes_log2  = (c_size_mult + 2U) + read_bl_len;
    return (c_size + 1U) << (bytes_log2 - 9U); /* bytes / 512 */
}

/*******************************************************************************
* Public API
*******************************************************************************/

sd_result_t sd_card_init(void)
{
    sd_result_t result;
    uint32_t resp;

    s_initialized = false;
    memset(&s_info, 0, sizeof(s_info));

    /* --- Steps 1 + 2: timer, pins, clocks ------------------------------- */
    sd_timer_init();
    sd_pins_init();
    sd_clock_init();

    /* --- Step 3: host controller setup ---------------------------------- */
    XMC_SDMMC_Enable(SD_HOST);                          /* ungate clock, release reset */
    sd_reset_lines((uint32_t)XMC_SDMMC_SW_RESET_ALL);   /* start from a clean state */

    /* The slot's card-detect and write-protect switches are not used, so
     * tell the host in software: "card is inserted, not write protected".
     * On XMC4700 these live in SCU register SDMMC_CON:
     *   CDSEL = 1  -> card detect comes from software (CDSVAL), not a pin
     *   CDSVAL = 0 -> card PRESENT. The value acts like a real, active-low
     *                 card-detect switch. (Found on the real board: CDSVAL = 1,
     *                 as XMCLib's enum suggests, means "no card", and the host
     *                 then switches off bus power and the SD clock.)
     *   WPSEL = 1, WPSVAL = 0 -> not write protected
     * The XMCLib helpers XMC_SDMMC_SetCardDetection*() only work on XMC4500,
     * and the device header's SDMMC_CON macro clashes with its own member
     * name, so we write the register address directly. */
    *(volatile uint32_t *)SDMMC_CON_BASE = SDMMC_CON_CDSEL_Msk | SDMMC_CON_WPSEL_Msk;

    /* Wait for the host's card-detect logic to finish debouncing before
     * powering the bus. */
    uint32_t cd_start = sd_now();
    while (!XMC_SDMMC_GetPresentState(SD_HOST).card_state_stable)
    {
        if (sd_elapsed(cd_start, SD_CMD_TIMEOUT_MS))
        {
            break; /* carry on; CMD0/ACMD41 will report the problem */
        }
    }

    XMC_SDMMC_EnableEventStatus(SD_HOST, SD_EVENTS_USED);
    XMC_SDMMC_SetDataLineTimeout(SD_HOST, XMC_SDMMC_DAT_TIMEOUT_COUNTER_2_POW_27);
    XMC_SDMMC_SetBusVoltage(SD_HOST, XMC_SDMMC_BUS_VOLTAGE_3_3_VOLTS);
    XMC_SDMMC_BusPowerOn(SD_HOST);
    XMC_SDMMC_SetDataTransferWidth(SD_HOST, XMC_SDMMC_DATA_LINES_1);
    XMC_SDMMC_DisableHighSpeed(SD_HOST);

    result = sd_set_clock(SD_INIT_CLK_DIV);
    if (result != SD_OK)
    {
        return result;
    }
    sd_delay_ms(10U); /* card power-up + at least 74 SD clocks before CMD0 */

    /* --- Step 4: card identification ------------------------------------ */

    /* CMD0: reset the card into the idle state. */
    result = sd_send_cmd(CMD0_GO_IDLE_STATE, 0U, RSP_NONE, false, NULL);
    if (result != SD_OK)
    {
        return result;
    }

    /* CMD8: "do you support 3.3 V?" Version 2.00+ cards answer and echo the
     * pattern; old version 1.x cards stay silent (command timeout). */
    bool v2_card = false;
    result = sd_send_cmd(CMD8_SEND_IF_COND, CMD8_ARG, RSP_R7, false, &resp);
    if (result == SD_OK)
    {
        if ((resp & 0xFFFU) != CMD8_ARG)
        {
            return SD_ERR_UNUSABLE;
        }
        v2_card = true;
    }
    else if (result != SD_ERR_TIMEOUT)
    {
        return result;
    }

    /* ACMD41: start card power-up; repeat until the card says it is done. */
    uint32_t acmd41_arg = OCR_VOLTAGE_WINDOW | (v2_card ? OCR_HCS_CCS : 0U);
    uint32_t start = sd_now();
    for (;;)
    {
        result = sd_send_acmd(ACMD41_SD_SEND_OP_COND, acmd41_arg, RSP_R3, &s_info.ocr);
        if (result == SD_ERR_TIMEOUT)
        {
            return SD_ERR_NO_CARD; /* nobody answered CMD55/ACMD41 */
        }
        if (result != SD_OK)
        {
            return result;
        }
        if ((s_info.ocr & OCR_POWER_UP_DONE) != 0U)
        {
            break;
        }
        if (sd_elapsed(start, SD_INIT_TIMEOUT_MS))
        {
            return SD_ERR_UNUSABLE;
        }
        sd_delay_ms(10U);
    }
    s_info.high_capacity = ((s_info.ocr & OCR_HCS_CCS) != 0U);

    /* CMD2: read the card's ID (manufacturer, name, serial number). */
    result = sd_send_cmd(CMD2_ALL_SEND_CID, 0U, RSP_R2, false, s_info.cid);
    if (result != SD_OK)
    {
        return result;
    }

    /* CMD3: the card picks a Relative Card Address that we use from now on. */
    result = sd_send_cmd(CMD3_SEND_RELATIVE_ADDR, 0U, RSP_R6, false, &resp);
    if (result != SD_OK)
    {
        return result;
    }
    s_info.rca = (uint16_t)(resp >> 16);

    /* CMD9: read the Card Specific Data to learn the capacity. */
    result = sd_send_cmd(CMD9_SEND_CSD, (uint32_t)s_info.rca << 16, RSP_R2, false, s_info.csd);
    if (result != SD_OK)
    {
        return result;
    }
    s_info.block_count = sd_csd_block_count(s_info.csd);

    /* CMD7: select the card -> it moves to the "transfer" state. */
    result = sd_send_cmd(CMD7_SELECT_CARD, (uint32_t)s_info.rca << 16, RSP_R1B, false, &resp);
    if (result != SD_OK)
    {
        return result;
    }

    /* --- Step 5: speed up --------------------------------------------- */

    result = sd_set_clock(SD_TRANSFER_CLK_DIV);
    if (result != SD_OK)
    {
        return result;
    }

    /* Make sure the card has finished CMD7 and is ready (CMD13). */
    result = sd_wait_card_ready();
    if (result != SD_OK)
    {
        return result;
    }

#if SD_USE_4BIT_BUS
    /* ACMD6: switch the card to a 4-bit bus, then the host. */
    result = sd_send_acmd(ACMD6_SET_BUS_WIDTH, ACMD6_BUS_WIDTH_4, RSP_R1, &resp);
    if (result != SD_OK)
    {
        return result;
    }
    if ((resp & R1_ERROR_BITS) != 0U)
    {
        return SD_ERR_CARD_STATUS;
    }
    XMC_SDMMC_SetDataTransferWidth(SD_HOST, XMC_SDMMC_DATA_LINES_4);
#endif

    /* CMD16: 512-byte blocks (only matters for old SDSC cards; SDHC is fixed). */
    if (!s_info.high_capacity)
    {
        result = sd_send_cmd(CMD16_SET_BLOCKLEN, SD_BLOCK_SIZE, RSP_R1, false, &resp);
        if (result != SD_OK)
        {
            return result;
        }
    }

    s_initialized = true;
    return SD_OK;
}

bool sd_card_is_initialized(void)
{
    return s_initialized;
}

const sd_card_info_t *sd_card_get_info(void)
{
    return &s_info;
}

/* Prepare the host for a single 512-byte block transfer. */
static void sd_setup_block_transfer(XMC_SDMMC_DATA_TRANSFER_DIR_t dir)
{
    XMC_SDMMC_TRANSFER_MODE_t mode =
    {
        .block_size = SD_BLOCK_SIZE,
        .num_blocks = 1U,
        .type       = XMC_SDMMC_TRANSFER_MODE_TYPE_SINGLE,
        .auto_cmd   = XMC_SDMMC_TRANSFER_MODE_AUTO_CMD_DISABLED,
        .direction  = dir,
    };
    XMC_SDMMC_SetDataTransferMode(SD_HOST, &mode);
    XMC_SDMMC_SetDataTransferDirection(SD_HOST, dir); /* SetDataTransferMode() ignores .direction */
}

/* SDSC cards are addressed in bytes, SDHC/SDXC cards in 512-byte blocks. */
static uint32_t sd_block_to_address(uint32_t block)
{
    return s_info.high_capacity ? block : (block * SD_BLOCK_SIZE);
}

static sd_result_t sd_read_one_block(uint32_t block, uint8_t *buf)
{
    uint32_t status;
    sd_result_t result = sd_wait_card_ready();
    if (result != SD_OK)
    {
        return result;
    }

    sd_setup_block_transfer(XMC_SDMMC_DATA_TRANSFER_CARD_TO_HOST);

    result = sd_send_cmd(CMD17_READ_SINGLE_BLOCK, sd_block_to_address(block), RSP_R1, true, &status);
    if (result == SD_OK && (status & R1_ERROR_BITS) != 0U)
    {
        result = SD_ERR_CARD_STATUS;
    }

    /* Wait for the host's 512-byte buffer to fill, then copy it out 32 bits at a time. */
    if (result == SD_OK)
    {
        result = sd_wait_event((uint16_t)XMC_SDMMC_BUFFER_READ_READY, SD_READ_TIMEOUT_MS);
    }
    if (result == SD_OK)
    {
        for (uint32_t i = 0U; i < (SD_BLOCK_SIZE / 4U); i++)
        {
            uint32_t word = XMC_SDMMC_ReadFIFO(SD_HOST);
            memcpy(&buf[i * 4U], &word, 4U); /* memcpy: 'buf' need not be 4-byte aligned */
        }
        result = sd_wait_event((uint16_t)XMC_SDMMC_TX_COMPLETE, SD_READ_TIMEOUT_MS);
    }

    if (result != SD_OK)
    {
        sd_reset_lines((uint32_t)XMC_SDMMC_SW_RST_CMD_LINE | (uint32_t)XMC_SDMMC_SW_RST_DAT_LINE);
    }
    return result;
}

static sd_result_t sd_write_one_block(uint32_t block, const uint8_t *buf)
{
    uint32_t status;
    sd_result_t result = sd_wait_card_ready();
    if (result != SD_OK)
    {
        return result;
    }

    sd_setup_block_transfer(XMC_SDMMC_DATA_TRANSFER_HOST_TO_CARD);

    result = sd_send_cmd(CMD24_WRITE_BLOCK, sd_block_to_address(block), RSP_R1, true, &status);
    if (result == SD_OK && (status & R1_ERROR_BITS) != 0U)
    {
        result = SD_ERR_CARD_STATUS;
    }

    /* Wait until the host buffer has room, fill it, then wait for the card
     * to finish receiving and programming the block. */
    if (result == SD_OK)
    {
        result = sd_wait_event((uint16_t)XMC_SDMMC_BUFFER_WRITE_READY, SD_WRITE_TIMEOUT_MS);
    }
    if (result == SD_OK)
    {
        for (uint32_t i = 0U; i < (SD_BLOCK_SIZE / 4U); i++)
        {
            uint32_t word;
            memcpy(&word, &buf[i * 4U], 4U);
            XMC_SDMMC_WriteFIFO(SD_HOST, &word);
        }
        result = sd_wait_event((uint16_t)XMC_SDMMC_TX_COMPLETE, SD_WRITE_TIMEOUT_MS);
    }

    if (result != SD_OK)
    {
        sd_reset_lines((uint32_t)XMC_SDMMC_SW_RST_CMD_LINE | (uint32_t)XMC_SDMMC_SW_RST_DAT_LINE);
    }
    return result;
}

static sd_result_t sd_check_range(uint32_t block, const void *buf, uint32_t count)
{
    if (!s_initialized)
    {
        return SD_ERR_NOT_INIT;
    }
    if ((buf == NULL) || (count == 0U) ||
        (block >= s_info.block_count) || (count > (s_info.block_count - block)))
    {
        return SD_ERR_PARAM;
    }
    return SD_OK;
}

sd_result_t sd_card_read_blocks(uint32_t block, uint8_t *buf, uint32_t count)
{
    sd_result_t result = sd_check_range(block, buf, count);
    for (uint32_t i = 0U; (result == SD_OK) && (i < count); i++)
    {
        result = sd_read_one_block(block + i, &buf[i * SD_BLOCK_SIZE]);
    }
    return result;
}

sd_result_t sd_card_write_blocks(uint32_t block, const uint8_t *buf, uint32_t count)
{
    sd_result_t result = sd_check_range(block, buf, count);
    for (uint32_t i = 0U; (result == SD_OK) && (i < count); i++)
    {
        result = sd_write_one_block(block + i, &buf[i * SD_BLOCK_SIZE]);
    }
    return result;
}

const char *sd_card_result_str(sd_result_t result)
{
    switch (result)
    {
        case SD_OK:              return "OK";
        case SD_ERR_NO_CARD:     return "no card detected";
        case SD_ERR_UNUSABLE:    return "card not supported";
        case SD_ERR_TIMEOUT:     return "timeout";
        case SD_ERR_CMD:         return "command error";
        case SD_ERR_DATA:        return "data transfer error";
        case SD_ERR_CARD_STATUS: return "card reported an error";
        case SD_ERR_PARAM:       return "bad parameter";
        case SD_ERR_NOT_INIT:    return "card not initialized";
        default:                 return "unknown error";
    }
}

/* [] END OF FILE */
