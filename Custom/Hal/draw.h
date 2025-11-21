#ifndef _DRAW_H
#define _DRAW_H

#include <stdint.h>
#include "cmsis_os2.h"
#include "dev_manager.h"
#include "fonts.h"
#include "aicam_error.h"
#include "stm32n6xx_hal.h"

#define COLOR_WHITE       0xFFFFFFFF  // Opaque white
#define COLOR_BLACK       0xFF000000  // Opaque black
#define COLOR_RED         0xFFFF0000  // Opaque red
#define COLOR_GREEN       0xFF00FF00  // Opaque green
#define COLOR_BLUE        0xFF0000FF  // Opaque blue
#define COLOR_YELLOW      0xFFFFFF00  // Opaque yellow
#define COLOR_CYAN        0xFF00FFFF  // Opaque cyan
#define COLOR_MAGENTA     0xFFFF00FF  // Opaque magenta
#define COLOR_GRAY        0xFF808080  // Opaque gray
#define COLOR_LIGHTGRAY   0xFFD3D3D3  // Opaque light gray
#define COLOR_DARKGRAY    0xFF404040  // Opaque dark gray
#define COLOR_TRANSPARENT 0x00000000  // Fully transparent

#define ARGB8888_TO_RGB565(c) \
    (uint16_t)((((c) >> 19) & 0x1F) << 11 | (((c) >> 10) & 0x3F) << 5 | (((c) >> 3) & 0x1F))

#define ARGB8888_TO_RGB888(c) \
    (uint32_t)((((c) >> 16) & 0xFF) << 16 | (((c) >> 8) & 0xFF) << 8 | ((c) & 0xFF))

#define ARGB8888_TO_ARGB1555(c) \
    (uint16_t)((((c) >> 31) << 15) | (((c) >> 19) & 0x1F) << 10 | (((c) >> 11) & 0x1F) << 5 | (((c) >> 3) & 0x1F))

#define ARGB8888_TO_ARGB4444(c) \
    (uint16_t)((((c) >> 28) & 0xF) << 12 | (((c) >> 20) & 0xF) << 8 | (((c) >> 12) & 0xF) << 4 | (((c) >> 4) & 0xF))

#define DRAW_DEFAULT_INPUT_COLORMODE DMA2D_INPUT_RGB565
#define DRAW_DEFAULT_OUTPUT_COLORMODE DMA2D_OUTPUT_RGB565

#define MAX_LINE_CHAR 64


typedef enum {
    DRAW_CMD_SET_COLOR_MODE = DRAW_CMD_BASE,
    DRAW_CMD_FONT_SETUP,
    DRAW_CMD_FILL,
    DRAW_CMD_RECT,
    DRAW_CMD_COPY,
    DRAW_CMD_PRINTF,
    DRAW_CMD_CLIP,
    DRAW_CMD_COLOR_CONVERT,
    DRAW_CMD_DOT,
    DRAW_CMD_LINE,
    DRAW_CMD_BLEND_COLOR_RECT,
}DRAW_CMD_E;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t *data;
} DRAW_Font_t;

typedef struct {
    sFONT *p_font_in;
    DRAW_Font_t *p_font;
} draw_fontsetup_param_t;

// Parameters for filling a rectangular area with a single color
typedef struct {
    uint8_t *p_dst;      // Pointer to the destination buffer
    int dst_width;       // Width of the destination buffer (pixels)
    int dst_height;      // Height of the destination buffer (pixels)
    int x_pos;           // X position (top-left) in the destination buffer
    int y_pos;           // Y position (top-left) in the destination buffer
    int width;           // Width of the area to fill (pixels)
    int height;          // Height of the area to fill (pixels)
    int line_width;      // line wide
    uint32_t color;      // Fill color (ARGB8888 format)
} draw_fill_param_t;

// Parameters for drawing a rectangle border
typedef struct {
    uint8_t *p_dst;      // Pointer to the destination buffer
    int dst_width;       // Width of the destination buffer (pixels)
    int dst_height;      // Height of the destination buffer (pixels)
    int x_pos;           // X position (top-left) of the rectangle
    int y_pos;           // Y position (top-left) of the rectangle
    int width;           // Rectangle width (pixels)
    int height;          // Rectangle height (pixels)
    int line_width;      // Border line  wide
    uint32_t color;      // Border color (ARGB8888 format)
} draw_rect_param_t;

// Parameters for copying a rectangular area from source to destination
typedef struct {
    uint8_t *p_dst;      // Pointer to the destination buffer
    int dst_width;       // Width of the destination buffer (pixels)
    int dst_height;      // Height of the destination buffer (pixels)
    uint8_t *p_src;      // Pointer to the source buffer
    int src_width;       // Width of the source buffer (pixels)
    int src_height;      // Height of the source buffer (pixels)
    int x_offset;        // X offset in destination buffer where source will be copied
    int y_offset;        // Y offset in destination buffer where source will be copied
} draw_copy_param_t;

// Parameters for drawing a formatted string to the buffer
typedef struct {
    DRAW_Font_t *p_font; // Pointer to the font structure
    uint8_t *p_dst;      // Pointer to the destination buffer
    int dst_width;       // Width of the destination buffer (pixels)
    int dst_height;      // Height of the destination buffer (pixels)
    int x_pos;           // X position to start drawing the string
    int y_pos;           // Y position to start drawing the string
    char str[MAX_LINE_CHAR];        // Null-terminated string to draw
} draw_printf_param_t;

// Parameters for copying a clipped (sub) region from source to destination
typedef struct {
    uint8_t *p_dst;      // Pointer to the destination buffer
    int dst_width;       // Width of the destination buffer (pixels)
    int dst_height;      // Height of the destination buffer (pixels)
    uint8_t *p_src;      // Pointer to the source buffer
    int src_width;       // Width of the source buffer (pixels)
    int src_height;      // Height of the source buffer (pixels)
    int src_x;           // X position (top-left) of the clipping region in the source buffer
    int src_y;           // Y position (top-left) of the clipping region in the source buffer
    int clip_width;      // Width of the clipping region (pixels)
    int clip_height;     // Height of the clipping region (pixels)
    int dst_x;           // X position in the destination buffer to place the clipped region
    int dst_y;           // Y position in the destination buffer to place the clipped region
} draw_clip_param_t;

// Draw dot parameters
typedef struct {
    uint8_t *p_dst;
    int dst_width;
    int dst_height;
    int x_pos;
    int y_pos;
    int dot_width;
    uint32_t color;
} draw_dot_param_t;

typedef struct {
    uint8_t *p_dst;
    int dst_width;
    int dst_height;
    int x1;
    int y1;
    int x2;
    int y2;
    int line_width;
    uint32_t color;
} draw_line_param_t;

typedef struct {
    uint32_t in_colormode;
    uint32_t out_colormode;
} draw_colormode_param_t;

typedef struct {
    uint32_t in_colormode;
    uint32_t out_colormode; 
    uint8_t *p_dst; 
    uint8_t *p_src; 
    int src_width; 
    int src_height;
    int rb_swap; // 0: not swap, 1: swap
    int ChromaSubSampling;
} draw_color_convert_param_t;

typedef struct {
    uint8_t *p_dst;      // Destination buffer start address
    int dst_width;       // Buffer width (pixels)
    int dst_height;      // Buffer height (pixels)
    int x_pos;           // Fill block top-left corner X
    int y_pos;           // Fill block top-left corner Y
    int width;           // Fill block width
    int height;          // Fill block height
    uint32_t color;      // Fill color (ARGB8888 format)
    uint8_t alpha;       // Transparency (0~255)
} draw_colorrect_param_t;

typedef struct {
    bool is_init;
    device_t *dev;
    osMutexId_t mtx_id;
    osSemaphoreId_t sem_id;
    osThreadId_t draw_processId;
    draw_colormode_param_t colormode_param;
} draw_t;

int draw_register(void);
int draw_unregister(void);
#endif