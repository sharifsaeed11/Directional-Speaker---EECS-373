/******************************************************************************
* File Name:   sd_card.h
*
* Description: Minimal SD card driver for the XMC4700 Relax Kit's on-board
*              microSD slot. It talks to the card through the XMC4700's SDMMC
*              host controller (4-bit SD bus) using only the XMCLib low-level
*              driver (xmc_sdmmc.h). No DMA, no interrupts: everything is
*              polled so it is easy to step through in the debugger.
*
*              This layer only knows how to move 512-byte blocks (sectors).
*              The FatFs glue in sd_diskio.c sits on top of it to give you
*              files and folders.
*
* EECS 373 example project.
******************************************************************************/

#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Size of one SD card block (sector) in bytes. Always 512 for SDSC/SDHC/SDXC. */
#define SD_BLOCK_SIZE   (512U)

/* Result codes returned by every sd_card_* function. */
typedef enum
{
    SD_OK = 0,          /* Success */
    SD_ERR_NO_CARD,     /* Card did not answer at all (missing / not seated) */
    SD_ERR_UNUSABLE,    /* Card answered but is not a supported SD card */
    SD_ERR_TIMEOUT,     /* Card or host did not finish in time */
    SD_ERR_CMD,         /* Command error (CRC, index, end bit, timeout) */
    SD_ERR_DATA,        /* Data transfer error (CRC, timeout, end bit) */
    SD_ERR_CARD_STATUS, /* Card reported an error in its status register */
    SD_ERR_PARAM,       /* Bad argument (e.g. block number past end of card) */
    SD_ERR_NOT_INIT     /* sd_card_init() has not succeeded yet */
} sd_result_t;

/* Information collected about the card during sd_card_init(). */
typedef struct
{
    bool     high_capacity; /* true = SDHC/SDXC (block addressed), false = SDSC (byte addressed) */
    uint16_t rca;           /* Relative Card Address assigned by the card */
    uint32_t block_count;   /* Total number of 512-byte blocks on the card */
    uint32_t ocr;           /* Operation Conditions Register */
    uint32_t cid[4];        /* Card IDentification register (raw, as read from the host) */
    uint32_t csd[4];        /* Card Specific Data register (raw, as read from the host) */
} sd_card_info_t;

/* Bring up the pins, clocks, and SDMMC peripheral, then run the SD card
 * identification sequence and switch the bus to 4-bit mode.
 * Safe to call again (e.g. after the card was swapped). */
sd_result_t sd_card_init(void);

/* true once sd_card_init() has succeeded. */
bool sd_card_is_initialized(void);

/* Read/write 'count' consecutive 512-byte blocks starting at block 'block'.
 * 'buf' may have any alignment and must hold count * 512 bytes. */
sd_result_t sd_card_read_blocks(uint32_t block, uint8_t *buf, uint32_t count);
sd_result_t sd_card_write_blocks(uint32_t block, const uint8_t *buf, uint32_t count);

/* Information about the card found by sd_card_init(). */
const sd_card_info_t *sd_card_get_info(void);

/* Human-readable name for a result code, handy for printf(). */
const char *sd_card_result_str(sd_result_t result);

/* Millisecond busy-wait delay (uses the Cortex-M4 DWT cycle counter). */
void sd_delay_ms(uint32_t ms);

#if defined(__cplusplus)
}
#endif

#endif /* SD_CARD_H */
