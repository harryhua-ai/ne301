/**
 * @file api_ota_module.c
 * @brief OTA API Module Implementation
 * @details OTA (Over-The-Air) upgrade API implementation
 */

#include "api_ota_module.h"
#include "web_api.h"
#include "ota_service.h"
#include "ota_header.h"
#include "debug.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "generic_file.h"
#include "buffer_mgr.h"
#include "drtc.h"
#include "storage.h"

#define OTA_WRITE_BUF_SIZE 1024
/* ==================== Global Variables ==================== */
static aicam_bool_t g_ota_upgrade_in_progress = AICAM_FALSE;
typedef struct {
    upgrade_handle_t handle;
    firmware_header_t header_storage;
    uint32_t remaining_size;
    char *buffer;
} ota_export_ctx_t;

typedef struct {
    union {
        ota_header_t data;
        uint64_t align_dummy; // force 8 bytes alignment
        uint8_t raw[sizeof(ota_header_t)];
    } header_storage;

    size_t total_received;          // total bytes received
    size_t content_length;          // HTTP Content-Length
    FirmwareType fw_type_param;     // firmware type specified in the URL

    size_t header_received;         // number of bytes received for the header
    aicam_bool_t header_processed;  // whether the header has been processed

    firmware_header_t fw_header;    // parsed header information
    uint32_t running_crc32;         // running CRC32
    upgrade_handle_t upgrade_handle;

    uint8_t write_buf[OTA_WRITE_BUF_SIZE];
    size_t write_buf_pos;

    aicam_bool_t failed;
    aicam_bool_t initialized;
} ota_upload_ctx_t;


/* ==================== Helper Functions ==================== */

/**
 * @brief Update CRC32 checksum
 * @param crc Current CRC32 value
 * @param data Data to update CRC32
 * @param len Length of data
 * @return Updated CRC32 value
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Check if OTA service is running
 */
static aicam_bool_t is_ota_service_running(void) {
    service_state_t state = ota_service_get_state();
    return (state == SERVICE_STATE_RUNNING);
}


/**
 * @brief Parse firmware type from string
 */
static FirmwareType parse_firmware_type(const char* type_str) {
    if (!type_str) return FIRMWARE_APP;
    
    if (strcmp(type_str, "fsbl") == 0) {
        return FIRMWARE_FSBL;
    } else if (strcmp(type_str, "app") == 0) {
        return FIRMWARE_APP;
    } else if (strcmp(type_str, "web") == 0) {
        return FIRMWARE_WEB;
    } else if (strcmp(type_str, "ai_default") == 0) {
        return FIRMWARE_DEFAULT_AI;
    } else if (strcmp(type_str, "ai") == 0) {
        return FIRMWARE_AI_1;
    } else if (strcmp(type_str, "reserved1") == 0) {
        return FIRMWARE_RESERVED1;
    } else if (strcmp(type_str, "reserved2") == 0) {
        return FIRMWARE_RESERVED2;
    } else {
        return FIRMWARE_APP; // Default
    }
}


/**
 * @brief Stream export callback
 */
static void ota_stream_export_cb(struct mg_connection *c, int ev, void *ev_data) {
    ota_export_ctx_t *ctx = (ota_export_ctx_t *)c->fn_data;

    if (ev == MG_EV_POLL || ev == MG_EV_WRITE) {
        // point to check if the send buffer is full
        if (c->send.len > 8192) return;

        if (ctx->remaining_size > 0) {
            // read 1KB at a time
            size_t chunk_size = (ctx->remaining_size > 1024) ? 1024 : ctx->remaining_size;

            uint32_t bytes_read = ota_upgrade_read_chunk(&ctx->handle, ctx->buffer, chunk_size);

            if (bytes_read > 0) {
                mg_send(c, ctx->buffer, bytes_read);
                ctx->remaining_size -= bytes_read;
            } else {
                // read error, terminate
                LOG_SVC_ERROR("Read error during export");
                c->is_draining = 1;
            }
        } else {
            // send completed
            LOG_SVC_INFO("Export completed successfully");
            c->is_draining = 1; // mark the connection as draining to close
        }
    } else if (ev == MG_EV_CLOSE) {
        // clean up resources
        if (ctx) {
            if (ctx->buffer) buffer_free(ctx->buffer);
            buffer_free(ctx);
            c->fn_data = NULL;
        }
    }
}


/**
 * @brief send response to the client
 * @param c Mongoose connection
 * @param status HTTP status code
 * @param msg Message
 */
static void ota_send_response(struct mg_connection *c, int status, const char* msg) {
    char buf[256];
    const char* status_msg = (status == 200) ? "OK" : ((status == 400) ? "Bad Request" : "Error");
    
    // build the JSON response body
    char json_body[128];
    if (status == 200) {
        snprintf(json_body, sizeof(json_body), "{\"success\":\"true\",\"message\":\"%s\"}", msg ? msg : "");
    } else {
        snprintf(json_body, sizeof(json_body), "{\"success\":\"false\",\"message\":\"%s\"}", msg ? msg : "");
    }

    // use snprintf to build the complete response, ensure the length is correct
    int len = snprintf(buf, sizeof(buf), 
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n" 
        "Connection: close\r\n"
        "\r\n"
        "%s", 
        status, status_msg, (int)strlen(json_body), json_body);

    if (len > 0) {
        mg_send(c, buf, len);
    }
    c->is_draining = 1; // send and then disconnect
}

static int flush_write_buffer(ota_upload_ctx_t *ctx) {
    if (ctx->write_buf_pos > 0) {
        if (ota_upgrade_write_chunk(&ctx->upgrade_handle, ctx->write_buf, ctx->write_buf_pos) != 0) {
            LOG_SVC_ERROR("Flash write failed (flush)");
            return -1;
        }
        ctx->write_buf_pos = 0; // reset the position
    }
    return 0;
}

/**
 * @brief process the OTA header verification and initialization
 * @param ctx OTA upload context
 * @return 0 on success, -1 on failure
 */
static int process_ota_header(ota_upload_ctx_t *ctx) {

    ota_header_t *header = &ctx->header_storage.data;

    // 1. check the firmware header
    if (ota_header_verify(header) != 0) {
        LOG_SVC_ERROR("Invalid firmware header magic/crc");
        return -1;
    }

    FirmwareType fw_type_from_header = FIRMWARE_APP;
    switch (header->fw_type) {
        case 0x01: fw_type_from_header = FIRMWARE_FSBL; break;
        case 0x02: fw_type_from_header = FIRMWARE_APP; break;
        case 0x03: fw_type_from_header = FIRMWARE_WEB; break;
        case 0x04: fw_type_from_header = FIRMWARE_AI_1; break;
        case 0x05: fw_type_from_header = FIRMWARE_AI_1; break;
        default: fw_type_from_header = FIRMWARE_APP; break;
    }

    LOG_SVC_INFO("Firmware type from header: %d, param: %d", fw_type_from_header, ctx->fw_type_param);
    if (fw_type_from_header != ctx->fw_type_param) {
        LOG_SVC_ERROR("Firmware type mismatch: header=%d, param=%d", fw_type_from_header, ctx->fw_type_param);
        return -1;
    }

    // 2. check the firmware size
    if (header->total_package_size != ctx->content_length) {
        LOG_SVC_ERROR("Firmware size mismatch: header=%u, http=%u", 
                     header->total_package_size, (uint32_t)ctx->content_length);
        return -1;
    }

    // 3. validate the firmware header options
    ctx->fw_header.file_size = header->total_package_size;
    memcpy(ctx->fw_header.version, header->fw_ver, sizeof(ctx->fw_header.version));
    ctx->fw_header.crc32 = header->fw_crc32;
    ctx->upgrade_handle.total_size = header->total_package_size;
    ctx->upgrade_handle.header = &ctx->fw_header;

    ota_validation_options_t options = {
        .validate_crc32 = AICAM_TRUE,
        .validate_signature = AICAM_FALSE,
        .validate_version = AICAM_FALSE, // usually set to FALSE in the development stage
        .validate_hardware = AICAM_TRUE,
        .validate_partition_size = AICAM_TRUE,
        .allow_downgrade = AICAM_FALSE,
        .min_version = 1,
        .max_version = 10
    };

    ota_validation_result_t val_res = ota_validate_firmware_header(&ctx->fw_header, ctx->fw_type_param, &options);
    if (val_res != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("Firmware header validation failed: %d", val_res);
        return -1;
    }

    // 4. validate the system state
    val_res = ota_validate_system_state(ctx->fw_type_param);
    if (val_res != OTA_VALIDATION_OK) {
        LOG_SVC_ERROR("System state validation failed: %d", val_res);
        return -1;
    }

    // 5. start the upgrade
    if (ota_upgrade_begin(&ctx->upgrade_handle, ctx->fw_type_param, &ctx->fw_header) != 0) {
        LOG_SVC_ERROR("upgrade_begin failed");
        return -1;
    }

    LOG_SVC_INFO("OTA Header Verified. Size: %u, CRC: 0x%08X. Writing...", 
                 ctx->fw_header.file_size, ctx->fw_header.crc32);
    
    return 0;
}

/* ==================== API Handlers for OTA ==================== */


void ota_upload_stream_processor(struct mg_connection *c, int ev, void *ev_data) {
    ota_upload_ctx_t *ctx = (ota_upload_ctx_t *)c->fn_data;

    if (ev == MG_EV_CLOSE || ev == MG_EV_ERROR) {
        if (ctx) {
            LOG_SVC_INFO("OTA upload cleanup (Event: %d)", ev);
            buffer_free(ctx);
            c->fn_data = NULL;
            // clear the global status
            g_ota_upgrade_in_progress = AICAM_FALSE;
        }
        return;
    }

    // -----------------------------
    // HTTP header parsing (initialization)
    // -----------------------------
    if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;

        /* Handle CORS preflight OPTIONS request */
        if (mg_match(hm->method, mg_str("OPTIONS"), NULL)) {
            mg_http_reply(c, 200,
                            "Access-Control-Allow-Origin: *\r\n"
                            "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                            "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                            "Access-Control-Max-Age: 86400\r\n",
                            "");
            return;
        }
        
        struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
        size_t total_len = cl ? (size_t)atol(cl->buf) : 0;
        
        if (total_len < sizeof(ota_header_t) || total_len > 100 * 1024 * 1024) { 
            ota_send_response(c, 400, "Invalid Content-Length");
            return;
        }

        if (g_ota_upgrade_in_progress) {
            ota_send_response(c, 400, "OTA already in progress");
            return;
        }

        char fw_type_str[32] = {0};
        if (mg_http_get_var(&hm->query, "firmwareType", fw_type_str, sizeof(fw_type_str)) <= 0) {
            strcpy(fw_type_str, "app"); 
        }

        ctx = (ota_upload_ctx_t *)buffer_calloc(1, sizeof(ota_upload_ctx_t));
        if (!ctx) {
            ota_send_response(c, 500, "OOM");
            return;
        }
        
        ctx->content_length = total_len;
        ctx->fw_type_param = parse_firmware_type(fw_type_str);
        ctx->initialized = AICAM_TRUE;
        ctx->running_crc32 = 0xFFFFFFFF; 
        c->fn_data = ctx; 

        LOG_SVC_INFO("OTA Stream Init: type=%d (%s), len=%u", 
                     ctx->fw_type_param, fw_type_str, (unsigned int)ctx->content_length);

        g_ota_upgrade_in_progress = AICAM_TRUE;

        // remove the Mongoose HTTP protocol handler, switch to Raw TCP mode
        c->pfn = NULL; 
        mg_iobuf_del(&c->recv, 0, hm->head.len); // delete the header

    }

    // -----------------------------
    // data processing (buffer header + write)
    // -----------------------------
    if (ctx && ctx->initialized && !ctx->failed && c->recv.len > 0) {
        size_t processed = 0;
        uint8_t *data = (uint8_t *)c->recv.buf;
        size_t len = c->recv.len;

        // A. header buffering stage
        if (!ctx->header_processed) {
            size_t needed = sizeof(ota_header_t) - ctx->header_received;
            size_t to_copy = (len < needed) ? len : needed;

            memcpy(ctx->header_storage.raw + ctx->header_received, data, to_copy);
            ctx->header_received += to_copy;
            processed += to_copy;

            // header received complete
            if (ctx->header_received == sizeof(ota_header_t)) {
                if (process_ota_header(ctx) != 0) {
                    ctx->failed = AICAM_TRUE;
                    ota_send_response(c, 400, "Header verification failed");
                    goto cleanup;
                }
                ctx->header_processed = AICAM_TRUE;

                // Header is also part of the firmware, FSBL is skipped, others need to be written
                if (ctx->fw_type_param != FIRMWARE_FSBL) {
                    memcpy(ctx->write_buf, ctx->header_storage.raw, sizeof(ota_header_t));
                    ctx->write_buf_pos = sizeof(ota_header_t);
                }
                // Header does not participate in the Payload CRC calculation, so here we don't update the CRC
            }
        }

        // B. data writing stage
        if (ctx->header_processed && processed < len) {
            uint8_t *payload = data + processed;
            size_t payload_len = len - processed;

            // 1. update the CRC32 
            ctx->running_crc32 = crc32_update(ctx->running_crc32, payload, payload_len);

            // 2. write to Flash 
            size_t remaining_payload = payload_len;
            while (remaining_payload > 0) {
                size_t space_left = OTA_WRITE_BUF_SIZE - ctx->write_buf_pos;
                size_t chunk = (remaining_payload < space_left) ? remaining_payload : space_left;

                memcpy(ctx->write_buf + ctx->write_buf_pos, payload, chunk);
                ctx->write_buf_pos += chunk;
                payload += chunk;
                remaining_payload -= chunk;

                // buffer is full, flush to Flash
                if (ctx->write_buf_pos == OTA_WRITE_BUF_SIZE) {
                    if (flush_write_buffer(ctx) != 0) {
                        ctx->failed = AICAM_TRUE;
                        ota_send_response(c, 500, "Flash write failed");
                        goto cleanup;
                    }
                }
            }
        }

        ctx->total_received += len; 
        

        // release the processed memory
        mg_iobuf_del(&c->recv, 0, len);

        // C. progress log
        if (ctx->total_received % (256 * 1024) < len || (ctx->total_received == ctx->content_length)) {
             uint32_t percent = (uint32_t)(ctx->total_received * 100 / ctx->content_length);
             LOG_SVC_INFO("OTA Progress: %u%% (%u / %u)", percent, (unsigned int)ctx->total_received, (unsigned int)ctx->content_length);
        }

        // -----------------------------
        // stage 3: end verification 
        // -----------------------------
        if (ctx->total_received >= ctx->content_length) {
            LOG_SVC_INFO("Transfer Complete. Finalizing...");

            if (flush_write_buffer(ctx) != 0) {
                ota_send_response(c, 500, "Flash flush failed");
                goto cleanup;
            }

            // Finalize CRC
            ctx->running_crc32 ^= 0xFFFFFFFF;

            // Verify CRC (Step 6 check)
            if (ctx->running_crc32 != ctx->fw_header.crc32) {
                LOG_SVC_ERROR("CRC32 mismatch: calc=0x%08X, header=0x%08X", 
                              ctx->running_crc32, ctx->fw_header.crc32);
                ota_send_response(c, 500, "CRC32 verification failed");
                goto cleanup;
            }

            // Finish upgrade
            if (ota_upgrade_finish(&ctx->upgrade_handle) != 0) {
                LOG_SVC_ERROR("upgrade_finish failed");
                ota_send_response(c, 500, "Upgrade finish failed");
                goto cleanup;
            }

            // Update json config
            if (ctx->fw_type_param == FIRMWARE_AI_1) {
                json_config_set_ai_1_active(AICAM_TRUE);
            }

            LOG_SVC_INFO("OTA Success!");
            ota_send_response(c, 200, "Upgrade successful");
            goto cleanup;
        }
        return;
    }

cleanup:
    if (ctx && (ctx->failed || ctx->total_received >= ctx->content_length)) {
        buffer_free(ctx);
        c->fn_data = NULL;
        g_ota_upgrade_in_progress = AICAM_FALSE; // should be handled in the service layer
    }
}

aicam_result_t ota_upload_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // return ok
    return api_response_success(ctx, NULL, "OTA upload handler called", 200, 0);
}

aicam_result_t ota_upgrade_local_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
   // return ok
   return api_response_success(ctx, NULL, "OTA upgrade local handler called", 200, 0);
}


aicam_result_t ota_export_firmware_handler(http_handler_context_t *ctx)
{
    if (!ctx) {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // only allow POST method
    if (!web_api_verify_method(ctx, "POST")) {
        return api_response_error(ctx, API_ERROR_METHOD_NOT_ALLOWED, "Only POST method is allowed");
    }
    
    // validate the Content-Type
    if (!web_api_verify_content_type(ctx, "application/json")) {
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid Content-Type");
    }
    
    // check if the OTA service is running
    if (!is_ota_service_running()) {
        return api_response_error(ctx, API_ERROR_SERVICE_UNAVAILABLE, "OTA service is not running");
    }
    
    LOG_SVC_INFO("OTA export firmware handler called");
    LOG_SVC_INFO("Request body size: %lu bytes", (unsigned long)ctx->request.content_length);
    
    // parse the request parameters
    cJSON *request = web_api_parse_body(ctx);
    if (!request) {
        LOG_SVC_ERROR("Failed to parse JSON request body");
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Invalid JSON request");
    }
    
    LOG_SVC_INFO("JSON request parsed successfully");
    
    // extract the parameters
    cJSON *type_item = cJSON_GetObjectItem(request, "firmware_type");
    cJSON *filename_item = cJSON_GetObjectItem(request, "filename");
    
    LOG_SVC_INFO("Extracted parameters:");
    LOG_SVC_INFO("  firmware_type: %s", type_item && cJSON_IsString(type_item) ? type_item->valuestring : "NULL");
    LOG_SVC_INFO("  filename: %s", filename_item && cJSON_IsString(filename_item) ? filename_item->valuestring : "NULL");
    
    if (!type_item || !cJSON_IsString(type_item)) {
        LOG_SVC_ERROR("Missing or invalid 'firmware_type' parameter");
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'firmware_type' parameter");
    }
    
    if (!filename_item || !cJSON_IsString(filename_item)) {
        LOG_SVC_ERROR("Missing or invalid 'filename' parameter");
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INVALID_REQUEST, "Missing or invalid 'filename' parameter");
    }
    
    // parse the parameters
    FirmwareType fw_type = parse_firmware_type(type_item->valuestring);
    if(fw_type == FIRMWARE_AI_1) {
        if(!json_config_get_ai_1_active()) {
            fw_type = FIRMWARE_DEFAULT_AI;
        }
    }
    char export_filename[256] = {0};
    memcpy(export_filename, filename_item->valuestring, strlen(filename_item->valuestring));
    
    LOG_SVC_INFO("Parsed parameters:");
    LOG_SVC_INFO("  firmware_type: %d (%s)", fw_type, type_item->valuestring);
    LOG_SVC_INFO("  export_filename: %s", export_filename);
    
    // get the current active slot
    SystemState *sys_state = ota_get_system_state();
    if (!sys_state) {
        LOG_SVC_ERROR("Failed to get system state");
        cJSON_Delete(request);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to get system state");
    }
    
    int slot_idx = sys_state->active_slot[fw_type];
    LOG_SVC_INFO("Current active slot for firmware type %d: %d", fw_type, slot_idx);
    
    cJSON_Delete(request);
    
    // get the slot information
    slot_info_t *slot_info = &sys_state->slot[fw_type][slot_idx];
    uint32_t firmware_size = slot_info->firmware_size;
    
    LOG_SVC_INFO("Starting firmware export: type=%d, slot=%d, size=%u bytes", fw_type, slot_idx, firmware_size);
    
    if (firmware_size == 0) {
        LOG_SVC_ERROR("Firmware size is 0 for type=%d, slot=%d", fw_type, slot_idx);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Firmware size is 0");
    }

    
    // prepare the export context
    ota_export_ctx_t *export_ctx = (ota_export_ctx_t *)buffer_calloc(1, sizeof(ota_export_ctx_t));
    if (!export_ctx) {
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate memory for export context");
    }
    
    memset(export_ctx, 0, sizeof(ota_export_ctx_t));
    export_ctx->handle.header = &export_ctx->header_storage;
    export_ctx->buffer = (char *)buffer_calloc(1, 1024); // allocate 1KB buffer
    if (!export_ctx->buffer) {
        buffer_free(export_ctx);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to allocate memory for export buffer");
    }

    //begin the export read
    if (ota_upgrade_read_begin(&export_ctx->handle, fw_type, slot_idx) != 0) {
        buffer_free(export_ctx->buffer);
        buffer_free(export_ctx);
        return api_response_error(ctx, API_ERROR_INTERNAL_ERROR, "Failed to begin read");
    }
    export_ctx->remaining_size = export_ctx->handle.total_size;

    //send response header
    mg_printf(ctx->conn, 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: %u\r\n"
        "Content-Disposition: attachment; filename=\"%s\"\r\n"
        "Connection: close\r\n" 
        "\r\n", 
        (unsigned int)export_ctx->remaining_size, export_filename);


    //set the callback function
    ctx->conn->fn = ota_stream_export_cb;
    ctx->conn->fn_data = export_ctx;
    
    LOG_SVC_INFO("Firmware export started: %s, %u bytes", export_filename, (unsigned int)export_ctx->remaining_size);
    
    return AICAM_ERROR_NOT_SENT_AGAIN;
}



/* ==================== Route Registration ==================== */

/**
 * @brief OTA API module routes
 */
static const api_route_t ota_module_routes[] = {
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/upload",
        .handler = ota_upload_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/upgrade-local",
        .handler = ota_upgrade_local_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    },
    {
        .method = "POST",
        .path = API_PATH_PREFIX "/system/ota/export",
        .handler = ota_export_firmware_handler,
        .require_auth = AICAM_TRUE,
        .user_data = NULL
    }
};

/**
 * @brief Register OTA API module
 */
aicam_result_t web_api_register_ota_module(void)
{
    LOG_SVC_INFO("Registering OTA API module...");
    
    // Register each route
    for (size_t i = 0; i < sizeof(ota_module_routes) / sizeof(ota_module_routes[0]); i++) {
        aicam_result_t result = http_server_register_route(&ota_module_routes[i]);
        if (result != AICAM_OK) {
            LOG_SVC_ERROR("Failed to register route %s: %d", ota_module_routes[i].path, result);
            return result;
        }
    }
    
    LOG_SVC_INFO("OTA API module registered successfully (%u routes)", 
                (unsigned int)sizeof(ota_module_routes) / sizeof(ota_module_routes[0]));
    
    return AICAM_OK;
}

