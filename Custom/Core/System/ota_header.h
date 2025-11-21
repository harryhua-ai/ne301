/**
 * @file ota_header.h
 * @brief OTA firmware package header structure definition
 * @version 1.0
 * @date 2025-10-15
 */

#ifndef OTA_HEADER_H
#define OTA_HEADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Header total size: 1024 bytes */
#define OTA_HEADER_SIZE         1024
#define OTA_MAGIC_NUMBER        0x4F544155  /* "OTAU" */
#define OTA_HEADER_VERSION      0x0100      /* v1.0 */
#define OTA_MAX_NAME_LEN        32
#define OTA_MAX_DESC_LEN        64
#define OTA_HASH_SIZE           32          /* SHA256 */
#define OTA_SIGNATURE_SIZE      256         /* RSA-2048 */
#define OTA_PARTITION_NAME_LEN  16
#define OTA_EXTENSION_COUNT     3
#define OTA_EXTENSION_KEY_LEN   32
#define OTA_EXTENSION_VAL_LEN   32

/* Firmware type constants */
#define OTA_FW_TYPE_UNKNOWN     0x00
#define OTA_FW_TYPE_FSBL        0x01        /* First Stage Boot Loader */
#define OTA_FW_TYPE_APP         0x02        /* Application */
#define OTA_FW_TYPE_WEB         0x03        /* Web Assets */
#define OTA_FW_TYPE_AI_MODEL    0x04        /* AI Model */
#define OTA_FW_TYPE_CONFIG      0x05        /* Configuration */
#define OTA_FW_TYPE_PATCH       0x06        /* Patch */
#define OTA_FW_TYPE_FULL        0x07        /* Full Package */

/* Encryption type constants */
#define OTA_ENCRYPT_NONE        0x00
#define OTA_ENCRYPT_AES128      0x01
#define OTA_ENCRYPT_AES256      0x02

/* Compression type constants */
#define OTA_COMPRESS_NONE       0x00
#define OTA_COMPRESS_GZIP       0x01
#define OTA_COMPRESS_LZ4        0x02

/**
 * @brief OTA firmware package header structure (1024 bytes)
 * @note All fields are plain types, no nested structures or enums
 */
typedef struct __attribute__((packed)) {
    /* ========== Basic Information Section (64 bytes) ========== */
    uint32_t magic;                         /* 0x00: Magic number "OTAU" (0x4F544155) */
    uint16_t header_version;                /* 0x04: Header version (0x0100 = v1.0) */
    uint16_t header_size;                   /* 0x06: Header size (1024) */
    uint32_t header_crc32;                  /* 0x08: Header CRC32 checksum */
    uint8_t  fw_type;                       /* 0x0C: Firmware type (see OTA_FW_TYPE_xxx) */
    uint8_t  encrypt_type;                  /* 0x0D: Encryption type (see OTA_ENCRYPT_xxx) */
    uint8_t  compress_type;                 /* 0x0E: Compression type (see OTA_COMPRESS_xxx) */
    uint8_t  reserved1;                     /* 0x0F: Reserved */
    uint32_t timestamp;                     /* 0x10: Creation timestamp (Unix time) */
    uint32_t sequence;                      /* 0x14: Sequence number */
    uint32_t total_package_size;            /* 0x18: Total package size (header + firmware) */
    uint8_t  reserved2[36];                 /* 0x1C-0x3F: Reserved (36 bytes) */
    
    /* ========== Firmware Information Section (160 bytes) ========== */
    char     fw_name[32];                   /* 0x40-0x5F: Firmware name */
    char     fw_desc[64];                   /* 0x60-0x9F: Firmware description */
    uint8_t  fw_ver[8];                     /* 0xA0-0xA7: Firmware version [major,minor,patch,build,0,0,0,0] */
    uint8_t  min_ver[8];                    /* 0xA8-0xAF: Minimum compatible version */
    uint32_t fw_size;                       /* 0xB0: Original firmware size */
    uint32_t fw_size_compressed;            /* 0xB4: Compressed firmware size */
    uint32_t fw_crc32;                      /* 0xB8: Firmware CRC32 checksum */
    uint8_t  fw_hash[32];                   /* 0xBC-0xDB: Firmware SHA256 hash */
    uint8_t  reserved3[4];                  /* 0xDC-0xDF: Reserved (4 bytes) */
    
    /* ========== Target Information Section (64 bytes) ========== */
    uint32_t target_addr;                   /* 0xE0: Target flash address */
    uint32_t target_size;                   /* 0xE4: Target region size */
    uint32_t target_offset;                 /* 0xE8: Target offset address */
    char     target_partition[16];          /* 0xEC-0xFB: Target partition name */
    uint32_t hw_version;                    /* 0xFC: Hardware version requirement */
    uint32_t chip_id;                       /* 0x100: Chip ID requirement */
    uint8_t  reserved4[28];                 /* 0x104-0x11F: Reserved (28 bytes) */
    
    /* ========== Dependency Information Section (64 bytes) ========== */
    uint8_t  reserved_dependencies[64];     /* 0x120-0x15F: Reserved for dependencies */
    
    /* ========== Security Information Section (416 bytes) ========== */
    uint8_t  reserved5[416];                /* 0x160-0x2FF: Reserved for security */
    
    /* ========== Extension Information Section (256 bytes) ========== */
    uint8_t  reserved6[256];                /* 0x300-0x3FF: Reserved for extensions (256 to make 1024 total) */
} ota_header_t;

/* Static assertion to ensure structure size is 1024 bytes */
_Static_assert(sizeof(ota_header_t) == OTA_HEADER_SIZE, "OTA header size must be 1024 bytes");

/**
 * @brief Verify header integrity
 * @param header Header structure pointer
 * @return 0 on success, others on failure
 */
int ota_header_verify(const ota_header_t *header);


#ifdef __cplusplus
}
#endif

#endif /* OTA_HEADER_H */
