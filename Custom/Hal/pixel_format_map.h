#ifndef PIXEL_FORMAT_MAP_H
#define PIXEL_FORMAT_MAP_H

#include <stdint.h>

int DCMIPP_BYTES_PER_PIXEL(uint32_t fmt) ;
int ENC_BYTES_PER_PIXEL(int fmt) ;
int DMA2D_BYTES_PER_PIXEL(uint32_t fmt) ;
float JPEG_BYTES_PER_PIXEL(uint32_t ChromaSubsampling) ;
int fmt_dcmipp_to_enc(uint32_t dcmipp_fmt) ;
uint32_t fmt_dcmipp_to_dma2d(uint32_t dcmipp_fmt) ;
uint32_t fmt_dma2d_to_enc(uint32_t out_dma2d) ;
uint32_t CSS_jpeg_to_dma2d(uint32_t ChromaSubsampling);
#endif