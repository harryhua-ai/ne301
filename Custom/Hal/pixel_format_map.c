#include "pixel_format_map.h"
#include "main.h"
#include "h264encapi.h"

typedef struct {
    uint32_t dcmipp_fmt;
    int      enc_fmt;
    uint32_t dma2d_input_fmt;
    uint32_t dma2d_output_fmt;
} PixelFormatMap;

static const PixelFormatMap pixel_format_map[] = {
    { DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1,        H264ENC_RGB565,   DMA2D_INPUT_RGB565, DMA2D_OUTPUT_RGB565},
    { DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1,        H264ENC_YUV422_INTERLEAVED_YUYV, 0xFFFFFFFF, 0xFFFFFFFF},
    { DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1_UYVY,   H264ENC_YUV422_INTERLEAVED_UYVY, 0xFFFFFFFF, 0xFFFFFFFF},
    { DCMIPP_PIXEL_PACKER_FORMAT_YUV420_2,        H264ENC_YUV420_PLANAR, 0xFFFFFFFF, 0xFFFFFFFF},
    { DCMIPP_PIXEL_PACKER_FORMAT_YUV420_3,        H264ENC_YUV420_PLANAR, 0xFFFFFFFF, 0xFFFFFFFF},
    { DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1, -1,               DMA2D_INPUT_RGB888, DMA2D_OUTPUT_RGB888},
    { DCMIPP_PIXEL_PACKER_FORMAT_ARGB8888,        H264ENC_RGB888,   DMA2D_INPUT_ARGB8888, DMA2D_OUTPUT_ARGB8888},
    { DCMIPP_PIXEL_PACKER_FORMAT_MONO_Y8_G8_1,    -1,               DMA2D_INPUT_L8, 0xFFFFFFFF},
    { DCMIPP_PIXEL_PACKER_FORMAT_YUV444_1,        -1,               DMA2D_INPUT_YCBCR, 0xFFFFFFFF},
};
#define PIXEL_FORMAT_MAP_SIZE (sizeof(pixel_format_map)/sizeof(pixel_format_map[0]))

int DCMIPP_BYTES_PER_PIXEL(uint32_t fmt) 
{
    switch(fmt) {
        case DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1:
        case DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1:
        case DCMIPP_PIXEL_PACKER_FORMAT_YUV422_1_UYVY:
        case DCMIPP_PIXEL_PACKER_FORMAT_YUV422_2:
            return 2;
        case DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1:
            return 3;
        case DCMIPP_PIXEL_PACKER_FORMAT_ARGB8888:
        case DCMIPP_PIXEL_PACKER_FORMAT_RGBA888:
        case DCMIPP_PIXEL_PACKER_FORMAT_YUV444_1:
            return 4;
        case DCMIPP_PIXEL_PACKER_FORMAT_MONO_Y8_G8_1:
            return 1;
        case DCMIPP_PIXEL_PACKER_FORMAT_YUV420_2:
        case DCMIPP_PIXEL_PACKER_FORMAT_YUV420_3:
            return 0;
        default:
            return 0;
    }
}


int ENC_BYTES_PER_PIXEL(int fmt) 
{
    switch(fmt) {
        case H264ENC_RGB565:
        case H264ENC_BGR565:
        case H264ENC_RGB555:
        case H264ENC_BGR555:
        case H264ENC_RGB444:
        case H264ENC_BGR444:
        case H264ENC_YUV422_INTERLEAVED_YUYV:
        case H264ENC_YUV422_INTERLEAVED_UYVY:
            return 2;
        case H264ENC_RGB888:
        case H264ENC_BGR888:
        case H264ENC_RGB101010:
        case H264ENC_BGR101010:
            return 4;
        case H264ENC_YUV420_PLANAR:
        case H264ENC_YUV420_SEMIPLANAR:
        case H264ENC_YUV420_SEMIPLANAR_VU:
            return 0;
        default:
            return 0;
    }
}

int DMA2D_BYTES_PER_PIXEL(uint32_t fmt) 
{
    switch(fmt) {
        case DMA2D_OUTPUT_RGB565:
        case DMA2D_OUTPUT_ARGB1555:
        case DMA2D_OUTPUT_ARGB4444:
            return 2;
        case DMA2D_OUTPUT_RGB888:
            return 3;
        case DMA2D_OUTPUT_ARGB8888:
            return 4;
        default:
            return 0;
    }
}

float JPEG_BYTES_PER_PIXEL(uint32_t ChromaSubsampling) 
{
    switch(ChromaSubsampling) {
        case JPEG_444_SUBSAMPLING:
            return 3;
        case JPEG_422_SUBSAMPLING:
            return 2;
        case JPEG_420_SUBSAMPLING:
            return 1.5;
        default:
            return 0;
    }
}

int fmt_dcmipp_to_enc(uint32_t dcmipp_fmt) 
{
    for (size_t i = 0; i < PIXEL_FORMAT_MAP_SIZE; ++i) {
        if (pixel_format_map[i].dcmipp_fmt == dcmipp_fmt)
            return pixel_format_map[i].enc_fmt;
    }
    return -1;
}

uint32_t fmt_dcmipp_to_dma2d(uint32_t dcmipp_fmt) 
{
    for (size_t i = 0; i < PIXEL_FORMAT_MAP_SIZE; ++i) {
        if (pixel_format_map[i].dcmipp_fmt == dcmipp_fmt)
            return pixel_format_map[i].dma2d_input_fmt;
    }
    return 0xFFFFFFFF;
}

uint32_t fmt_dma2d_to_enc(uint32_t out_dma2d) 
{
    for (size_t i = 0; i < PIXEL_FORMAT_MAP_SIZE; ++i) {
        if (pixel_format_map[i].dma2d_output_fmt == out_dma2d)
            return pixel_format_map[i].enc_fmt;
    }
    return -1;
}

uint32_t CSS_jpeg_to_dma2d(uint32_t ChromaSubsampling)
{
    switch(ChromaSubsampling) {
        case JPEG_444_SUBSAMPLING:
            return DMA2D_NO_CSS;
        case JPEG_422_SUBSAMPLING:
            return DMA2D_CSS_422;
        case JPEG_420_SUBSAMPLING:
            return DMA2D_CSS_420;
        default:
            return -1;
    }
}