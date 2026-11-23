/******************************************************************************
* File Name:   sd_diskio.c
*
* Description: FatFs "disk I/O" glue. FatFs (in the FatFs/ folder) is a
*              generic FAT filesystem that knows nothing about hardware; it
*              calls the five disk_* functions below whenever it needs to read
*              or write a sector. We implement them with the SD card driver.
*
*              This file replaces the template diskio.c that ships with FatFs.
*
* EECS 373 example project.
******************************************************************************/

#include "ff.h"
#include "diskio.h"
#include "sd_card.h"

/* We only have one drive: physical drive 0 = the microSD slot. */
#define SD_DRIVE    (0U)

static DRESULT sd_to_dresult(sd_result_t result)
{
    switch (result)
    {
        case SD_OK:           return RES_OK;
        case SD_ERR_PARAM:    return RES_PARERR;
        case SD_ERR_NOT_INIT: return RES_NOTRDY;
        default:              return RES_ERROR;
    }
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SD_DRIVE)
    {
        return STA_NOINIT;
    }
    return sd_card_is_initialized() ? 0U : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != SD_DRIVE)
    {
        return STA_NOINIT;
    }
    return (sd_card_init() == SD_OK) ? 0U : STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_DRIVE)
    {
        return RES_PARERR;
    }
    return sd_to_dresult(sd_card_read_blocks((uint32_t)sector, buff, count));
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SD_DRIVE)
    {
        return RES_PARERR;
    }
    return sd_to_dresult(sd_card_write_blocks((uint32_t)sector, buff, count));
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != SD_DRIVE)
    {
        return RES_PARERR;
    }
    if (!sd_card_is_initialized())
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
        case CTRL_SYNC:
            /* Every write in sd_card.c waits until the card is done, so there
             * is never anything left to flush. */
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = (LBA_t)sd_card_get_info()->block_count;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = (WORD)SD_BLOCK_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1U; /* erase block size unknown */
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

/* [] END OF FILE */
