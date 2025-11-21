/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef OS04C10_H
#define OS04C10_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "os04c10_reg.h"
#include <stddef.h>

/** @addtogroup BSP
  * @{
  */

/** @addtogroup Components
  * @{
  */

/** @addtogroup os04c10
  * @{
  */

/** @defgroup OS04C10_Exported_Types
  * @{
  */

typedef int32_t (*OS04C10_Init_Func)(void);
typedef int32_t (*OS04C10_DeInit_Func)(void);
typedef int32_t (*OS04C10_GetTick_Func)(void);
typedef void (*OS04C10_Delay_Func)(uint32_t);
typedef int32_t (*OS04C10_WriteReg_Func)(uint16_t, uint16_t, uint8_t *, uint16_t);
typedef int32_t (*OS04C10_ReadReg_Func)(uint16_t, uint16_t, uint8_t *, uint16_t);

typedef struct
{
  OS04C10_Init_Func          Init;
  OS04C10_DeInit_Func        DeInit;
  uint16_t                   Address;
  OS04C10_WriteReg_Func      WriteReg;
  OS04C10_ReadReg_Func       ReadReg;
  OS04C10_GetTick_Func       GetTick;
  OS04C10_Delay_Func         Delay;
} OS04C10_IO_t;


typedef struct
{
  OS04C10_IO_t        IO;
  os04c10_ctx_t       Ctx;
  uint8_t             IsInitialized;
  uint8_t             Mode;
  uint32_t            VirtualChannelID;
} OS04C10_Object_t;

typedef struct
{
  uint8_t FrameStartCode; /*!< Specifies the code of the frame start delimiter. */
  uint8_t LineStartCode;  /*!< Specifies the code of the line start delimiter.  */
  uint8_t LineEndCode;    /*!< Specifies the code of the line end delimiter.    */
  uint8_t FrameEndCode;   /*!< Specifies the code of the frame end delimiter.   */

} OS04C10_SyncCodes_t;

typedef struct
{
  uint32_t Config_Resolution;
  uint32_t Config_LightMode;
  uint32_t Config_SpecialEffect;
  uint32_t Config_Brightness;
  uint32_t Config_Saturation;
  uint32_t Config_Contrast;
  uint32_t Config_HueDegree;
  uint32_t Config_Gain;
  uint32_t Config_Exposure;
  uint32_t Config_MirrorFlip;
  uint32_t Config_Zoom;
  uint32_t Config_NightMode;
} OS04C10_Capabilities_t;

typedef struct
{
  int32_t (*Init)(OS04C10_Object_t *, uint32_t, uint32_t);
  int32_t (*DeInit)(OS04C10_Object_t *);
  int32_t (*ReadID)(OS04C10_Object_t *, uint32_t *);
  int32_t (*GetCapabilities)(OS04C10_Object_t *, OS04C10_Capabilities_t *);
  int32_t (*SetLightMode)(OS04C10_Object_t *, uint32_t);
  int32_t (*SetColorEffect)(OS04C10_Object_t *, uint32_t);
  int32_t (*SetBrightness)(OS04C10_Object_t *, int32_t);
  int32_t (*SetSaturation)(OS04C10_Object_t *, int32_t);
  int32_t (*SetContrast)(OS04C10_Object_t *, int32_t);
  int32_t (*SetHueDegree)(OS04C10_Object_t *, int32_t);
  int32_t (*MirrorFlipConfig)(OS04C10_Object_t *, uint32_t);
  int32_t (*ZoomConfig)(OS04C10_Object_t *, uint32_t);
  int32_t (*SetResolution)(OS04C10_Object_t *, uint32_t);
  int32_t (*GetResolution)(OS04C10_Object_t *, uint32_t *);
  int32_t (*SetPixelFormat)(OS04C10_Object_t *, uint32_t);
  int32_t (*GetPixelFormat)(OS04C10_Object_t *, uint32_t *);
  int32_t (*NightModeConfig)(OS04C10_Object_t *, uint32_t);
  int32_t (*SetFrequency  )(OS04C10_Object_t*, int32_t);
  int32_t (*SetGain       )(OS04C10_Object_t*, int32_t);
  int32_t (*SetExposure   )(OS04C10_Object_t*, int32_t);
  int32_t (*SetFramerate  )(OS04C10_Object_t*, int32_t);
} OS04C10_CAMERA_Drv_t;
/**
  * @}
  */

/** @defgroup OS04C10_Exported_Constants
  * @{
  */
#define OS04C10_OK                      (0)
#define OS04C10_ERROR                   (-1)


#define OS04C10_R1920_1080                5U	/* 2592x1944 Resolution       */

/**
  * @brief  OS04C10 Features Parameters
  */
/* Camera resolutions */
#define OS04C10_R160x120                 0x00U   /* QQVGA Resolution           */
#define OS04C10_R320x240                 0x01U   /* QVGA Resolution            */
#define OS04C10_R480x272                 0x02U   /* 480x272 Resolution         */
#define OS04C10_R640x480                 0x03U   /* VGA Resolution             */
#define OS04C10_R800x480                 0x04U   /* WVGA Resolution            */
#define OS04C10_R1920x1080               0x05U 
#define OS04C10_R2688x1520               0x06U

/* Camera Pixel Format */
#define OS04C10_RGB565                   0x00U   /* Pixel Format RGB565        */
#define OS04C10_RGB888                   0x01U   /* Pixel Format RGB888        */
#define OS04C10_YUV422                   0x02U   /* Pixel Format YUV422        */
#define OS04C10_Y8                       0x07U   /* Pixel Format Y8            */
#define OS04C10_JPEG                     0x08U   /* Compressed format JPEG          */

/* Polarity */
#define OS04C10_POLARITY_PCLK_LOW        0x00U /* Signal Active Low          */
#define OS04C10_POLARITY_PCLK_HIGH       0x01U /* Signal Active High         */
#define OS04C10_POLARITY_HREF_LOW        0x00U /* Signal Active Low          */
#define OS04C10_POLARITY_HREF_HIGH       0x01U /* Signal Active High         */
#define OS04C10_POLARITY_VSYNC_LOW       0x01U /* Signal Active Low          */
#define OS04C10_POLARITY_VSYNC_HIGH      0x00U /* Signal Active High         */

/* Mirror/Flip */
#define OS04C10_MIRROR_FLIP_NONE         0x00U   /* Set camera normal mode     */
#define OS04C10_FLIP                     0x01U   /* Set camera flip config     */
#define OS04C10_MIRROR                   0x02U   /* Set camera mirror config   */
#define OS04C10_MIRROR_FLIP              0x03U   /* Set camera mirror and flip */

/* Zoom */
#define OS04C10_ZOOM_x8                  0x00U   /* Set zoom to x8             */
#define OS04C10_ZOOM_x4                  0x11U   /* Set zoom to x4             */
#define OS04C10_ZOOM_x2                  0x22U   /* Set zoom to x2             */
#define OS04C10_ZOOM_x1                  0x44U   /* Set zoom to x1             */

/* Special Effect */
#define OS04C10_COLOR_EFFECT_NONE        0x00U   /* No effect                  */
#define OS04C10_COLOR_EFFECT_BLUE        0x01U   /* Blue effect                */
#define OS04C10_COLOR_EFFECT_RED         0x02U   /* Red effect                 */
#define OS04C10_COLOR_EFFECT_GREEN       0x04U   /* Green effect               */
#define OS04C10_COLOR_EFFECT_BW          0x08U   /* Black and White effect     */
#define OS04C10_COLOR_EFFECT_SEPIA       0x10U   /* Sepia effect               */
#define OS04C10_COLOR_EFFECT_NEGATIVE    0x20U   /* Negative effect            */


/* Light Mode */
#define OS04C10_LIGHT_AUTO               0x00U   /* Light Mode Auto            */
#define OS04C10_LIGHT_SUNNY              0x01U   /* Light Mode Sunny           */
#define OS04C10_LIGHT_OFFICE             0x02U   /* Light Mode Office          */
#define OS04C10_LIGHT_HOME               0x04U   /* Light Mode Home            */
#define OS04C10_LIGHT_CLOUDY             0x08U   /* Light Mode Claudy          */

/* Night Mode */
#define NIGHT_MODE_DISABLE              0x00U   /* Disable night mode         */
#define NIGHT_MODE_ENABLE               0x01U   /* Enable night mode          */

/* Colorbar Mode */
#define COLORBAR_MODE_DISABLE           0x00U   /* Disable colorbar mode      */
#define COLORBAR_MODE_ENABLE            0x01U   /* 8 bars W/Y/C/G/M/R/B/Bl    */
#define COLORBAR_MODE_GRADUALV          0x02U   /* Gradual vertical colorbar  */

/* Pixel Clock */
#define OS04C10_PCLK_7M                  0x00U   /* Pixel Clock set to 7Mhz    */
#define OS04C10_PCLK_8M                  0x01U   /* Pixel Clock set to 8Mhz    */
#define OS04C10_PCLK_9M                  0x02U   /* Pixel Clock set to 9Mhz    */
#define OS04C10_PCLK_12M                 0x04U   /* Pixel Clock set to 12Mhz   */
#define OS04C10_PCLK_24M                 0x08U   /* Pixel Clock set to 24Mhz   */
#define OS04C10_PCLK_48M                 0x09U   /* Pixel Clock set to 48MHz   */

/* Mode */
#define PARALLEL_MODE                   0x00U   /* Parallel Interface Mode */
#define SERIAL_MODE                     0x01U   /* Serial Interface Mode   */

/**
  * @}
  */

/** @defgroup OS04C10_Exported_Functions OS04C10 Exported Functions
  * @{
  */
int32_t OS04C10_RegisterBusIO(OS04C10_Object_t *pObj, OS04C10_IO_t *pIO);
int32_t OS04C10_Init(OS04C10_Object_t *pObj, uint32_t Resolution, uint32_t PixelFormat);
int32_t OS04C10_DeInit(OS04C10_Object_t *pObj);
int32_t OS04C10_ReadID(OS04C10_Object_t *pObj, uint32_t *Id);
int32_t OS04C10_GetCapabilities(OS04C10_Object_t *pObj, OS04C10_Capabilities_t *Capabilities);
int32_t OS04C10_SetLightMode(OS04C10_Object_t *pObj, uint32_t LightMode);
int32_t OS04C10_SetColorEffect(OS04C10_Object_t *pObj, uint32_t Effect);
int32_t OS04C10_SetBrightness(OS04C10_Object_t *pObj, int32_t Level);
int32_t OS04C10_SetSaturation(OS04C10_Object_t *pObj, int32_t Level);
int32_t OS04C10_SetContrast(OS04C10_Object_t *pObj, int32_t Level);
int32_t OS04C10_SetHueDegree(OS04C10_Object_t *pObj, int32_t Degree);
int32_t OS04C10_MirrorFlipConfig(OS04C10_Object_t *pObj, uint32_t Config);
int32_t OS04C10_ZoomConfig(OS04C10_Object_t *pObj, uint32_t Zoom);
int32_t OS04C10_SetResolution(OS04C10_Object_t *pObj, uint32_t Resolution);
int32_t OS04C10_GetResolution(OS04C10_Object_t *pObj, uint32_t *Resolution);
int32_t OS04C10_SetPixelFormat(OS04C10_Object_t *pObj, uint32_t PixelFormat);
int32_t OS04C10_GetPixelFormat(OS04C10_Object_t *pObj, uint32_t *PixelFormat);
int32_t OS04C10_SetPolarities(OS04C10_Object_t *pObj, uint32_t PclkPolarity, uint32_t HrefPolarity,
                             uint32_t VsyncPolarity);
int32_t OS04C10_GetPolarities(OS04C10_Object_t *pObj, uint32_t *PclkPolarity, uint32_t *HrefPolarity,
                             uint32_t *VsyncPolarity);
int32_t OS04C10_NightModeConfig(OS04C10_Object_t *pObj, uint32_t Cmd);
int32_t OS04C10_SetGain(OS04C10_Object_t *pObj, int32_t gain);
int32_t OS04C10_SetExposure(OS04C10_Object_t *pObj, int32_t exposure);
int32_t OS04C10_SetFrequency(OS04C10_Object_t *pObj, int32_t frequency);
int32_t OS04C10_SetFramerate(OS04C10_Object_t *pObj, int32_t framerate);
int32_t OS04C10_ColorbarModeConfig(OS04C10_Object_t *pObj, uint32_t Cmd);
int32_t OS04C10_EmbeddedSynchroConfig(OS04C10_Object_t *pObj, OS04C10_SyncCodes_t *pSyncCodes);
int32_t OS04C10_SetPCLK(OS04C10_Object_t *pObj, uint32_t ClockValue);

int OS04C10_EnableDVPMode(OS04C10_Object_t *pObj);
int32_t OS04C10_EnableMIPIMode(OS04C10_Object_t *pObj);
int32_t OS04C10_SetMIPIVirtualChannel(OS04C10_Object_t *pObj, uint32_t vchannel);
int32_t OS04C10_Start(OS04C10_Object_t *pObj);
int32_t OS04C10_Stop(OS04C10_Object_t *pObj);

/* CAMERA driver structure */
extern OS04C10_CAMERA_Drv_t   OS04C10_CAMERA_Driver;
/**
  * @}
  */
#ifdef __cplusplus
}
#endif

#endif /* OS04C10_H */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
