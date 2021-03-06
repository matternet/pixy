#ifndef HELPER_COMMANDS
#define HELPER_COMMANDS

// frame should be at least 64000 bytes
int pixy_cam_get_frame(uint8_t mode, uint16_t xoffset, uint16_t yoffset, uint16_t width, uint16_t height, uint8_t *frame);

int pixy_read_blocks(uint32_t block_start, uint32_t block_count, uint8_t *buffer);

#endif
