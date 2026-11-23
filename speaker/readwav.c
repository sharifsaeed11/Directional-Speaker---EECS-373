#include "ff.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "readwav.h"

FIL fil;
FRESULT res;
UINT bytes_read;

void open_wav(char* filename, uint32_t byte_offset){

    // 1. Open the WAV file
    res = f_open(&fil, filename, FA_READ);
    if (res != FR_OK) {
        // Handle open error
        return;
    }

    // 2. Move the file pointer to your desired starting position
    res = f_lseek(&fil, byte_offset);
    if (res != FR_OK || f_tell(&fil) != byte_offset) {
        // Handle seek error or offset past end-of-file
        f_close(&fil);
        return;
    }

}

void close_wav(){
    // 4. Close the file
    f_close(&fil);
}

void read_wav_segment(char* filename, uint32_t byte_offset, uint32_t bytes_to_read, int16_t * audio_buffer) {
    // FIL fil;
    // FRESULT res;
    // UINT bytes_read;

    // // 1. Open the WAV file
    // res = f_open(&fil, filename, FA_READ);
    // if (res != FR_OK) {
    //     // Handle open error
    //     return;
    // }

    // // 2. Move the file pointer to your desired starting position
    // res = f_lseek(&fil, byte_offset);
    // if (res != FR_OK || f_tell(&fil) != byte_offset) {
    //     // Handle seek error or offset past end-of-file
    //     f_close(&fil);
    //     return;
    // }

    // 3. Read the segment in chunks (or all at once if it fits in RAM)
    uint32_t remaining = bytes_to_read;
    uint32_t index = 0;
    int16_t buff[2];
    // uint8_t buff[4];
    while (remaining > 0) {
        UINT chunk = (remaining > sizeof(audio_buffer)) ? sizeof(audio_buffer) : remaining;
        
        // res = f_read(&fil, &audio_buffer[index], chunk, &bytes_read);
        res = f_read(&fil, buff, chunk, &bytes_read);
        // res = f_read(&fil, audio_buffer[index], chunk, &bytes_read);
        if (res != FR_OK || bytes_read == 0) {
            break; // Read error or reached EOF
        }
        audio_buffer[index] = buff[0];
        audio_buffer[index + 1] = buff[1];
        // audio_buffer[index + 2] = buff[2];
        // audio_buffer[index + 3] = buff[3];
        index += 2;
        // index += 4;
        remaining -= bytes_read;
    }

    // // 4. Close the file
    // f_close(&fil);
}

uint32_t wav_get_size(char* filename) {
    FIL fil;
    FRESULT res;
    uint32_t file_size;
    UINT bytes_read;

    // 1. Open the WAV file
    res = f_open(&fil, filename, FA_READ);
    if (res != FR_OK) {
        // Handle open error
        return 0;
    }

    // 2. Move the file pointer to your desired starting position
    res = f_lseek(&fil, 4);
    if (res != FR_OK || f_tell(&fil) != 4) {
        // Handle seek error or offset past end-of-file
        f_close(&fil);
        return 0;
    }

    // 3. Read the file size.
    res = f_read(&fil, &file_size, 4, &bytes_read);
    if (res != FR_OK) {
        return 0;
    }

    // 4. Close the file
    f_close(&fil);

    return file_size + 8;
}