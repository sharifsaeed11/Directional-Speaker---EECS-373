#include <stdint.h>
void open_wav(char* filename, uint32_t byte_offset);
void close_wav();
void read_wav_segment(char* filename, uint32_t byte_offset, uint32_t bytes_to_read, int16_t * audio_buffer);
uint32_t wav_get_size(char* filename);