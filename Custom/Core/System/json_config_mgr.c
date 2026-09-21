/**
 * @file json_config_mgr.c
 * @brief AI Camera JSON Configuration Management System Implementation
 * @details This file contains the public API implementation and global context
 * for the configuration manager. It delegates work to internal
 * modules (nvs, json, utils).
 */

 #include "json_config_internal.h" // Includes all necessary headers
#include "board_hw.h"
 #include "netif_manager.h"
 #include "buffer_mgr.h"
 #include "version.h"              // Centralized version info
 #include "fsbl_app_common.h"
#include "cmsis_os2.h"

 /* ==================== Internal Data Structures and Variables ==================== */
 
 // Definition of the global context
json_config_mgr_context_t g_json_config_ctx = {0};

static volatile uint32_t g_config_seq = 0;

static osMutexId_t g_json_config_write_mutex = NULL;

static aicam_bool_t json_config_write_lock(void)
{
    if (!g_json_config_write_mutex) return AICAM_TRUE;
    return (osMutexAcquire(g_json_config_write_mutex, osWaitForever) == osOK) ? AICAM_TRUE : AICAM_FALSE;
}

static void json_config_write_unlock(void)
{
    if (g_json_config_write_mutex) osMutexRelease(g_json_config_write_mutex);
}

#include "cfg_txn.h"

cfg_txn_t g_json_config_txn = {
    &g_json_config_ctx.current_config,
    &g_config_seq,
    sizeof(aicam_global_config_t)
};

#include "cfg_blob_store.h"
#include "cfg_config_cache.h"
#include "cfg_config_cache_nvs.h"
#include "cfg_writer_gate.h"

static cfg_blob_store_t g_json_config_blob;

static void json_config_device_info_mac_patch(void *member, size_t n, void *user)
{
    (void)n;
    device_info_config_t *info = (device_info_config_t *)member;
    const char *mac = (const char *)user;
    strncpy(info->mac_address, mac, sizeof(info->mac_address) - 1);
    info->mac_address[sizeof(info->mac_address) - 1] = '\0';
    if (strcmp(info->device_name, "AICAM-000000") == 0)
    {
        json_config_generate_device_name_from_mac(info->device_name,
                                                  sizeof(info->device_name), mac);
    }
}

static void json_config_password_patch(void *member, size_t n, void *user)
{
    (void)n;
    auth_mgr_config_t *auth = (auth_mgr_config_t *)member;
    const char *password = (const char *)user;
    strncpy(auth->admin_password, password, sizeof(auth->admin_password) - 1);
    auth->admin_password[sizeof(auth->admin_password) - 1] = '\0';
}

#define LC_CFG_STORE_PATH "/config/config_store.bin"

static aicam_result_t json_config_blob_read(void *user, uint32_t offset, void *buf, uint32_t len)
{
    (void)user;
    void *fd = flash_lfs_fopen(LC_CFG_STORE_PATH, "r");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fseek(fd, (long)offset, 0) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    int n = flash_lfs_fread(fd, buf, len);
    flash_lfs_fclose(fd);
    return (n == (int)len) ? AICAM_OK : AICAM_ERROR_IO;
}

static aicam_result_t json_config_blob_write(void *user, uint32_t offset, const void *buf,
                                             uint32_t len)
{
    (void)user;
    void *fd = flash_lfs_fopen(LC_CFG_STORE_PATH, "r+");
    if (!fd) fd = flash_lfs_fopen(LC_CFG_STORE_PATH, "w");
    if (!fd) return AICAM_ERROR_IO;
    if (flash_lfs_fseek(fd, (long)offset, 0) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fwrite(fd, buf, len) != (int)len) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fflush(fd) != 0) {
        flash_lfs_fclose(fd);
        return AICAM_ERROR_IO;
    }
    if (flash_lfs_fclose(fd) != 0) return AICAM_ERROR_IO;
    return AICAM_OK;
}

static aicam_bool_t json_config_txn_lock_fn(void *ctx)
{
    (void)ctx;
    return json_config_write_lock();
}

static void json_config_txn_unlock_fn(void *ctx)
{
    (void)ctx;
    json_config_write_unlock();
}

static const cfg_txn_lock_t g_json_config_txn_lock = {
    NULL,
    json_config_txn_lock_fn,
    json_config_txn_unlock_fn
};

static aicam_global_config_t g_json_config_commit_scratch;
static cfg_writer_gate_t g_json_config_gate = {
    NULL,
    json_config_txn_lock_fn,
    json_config_txn_unlock_fn,
    CFG_GATE_UNINITIALIZED
};

static aicam_result_t json_config_persist_blob(void *user, const void *candidate, size_t n,
                                               uint32_t *generation_out)
{
    (void)user;
    if (n != sizeof(aicam_global_config_t)) return AICAM_ERROR_INVALID_PARAM;
    aicam_result_t r = cfg_blob_store_save(&g_json_config_blob, candidate);
    if (r == AICAM_OK && generation_out) *generation_out = g_json_config_blob.generation;
    return r;
}

static void json_config_fill_view(const aicam_global_config_t *config, uint32_t gen,
                                  cfg_derived_view_t *view)
{
    memset(view, 0, sizeof(*view));
    view->authoritative_generation = gen;
    view->log_config = config->log_config;
    view->ai_debug = config->ai_debug;
    view->device_service = config->device_service;
}

static void json_config_derived_post_commit(void *user, const void *committed, size_t size,
                                            uint32_t generation)
{
    (void)user;
    if (size != sizeof(aicam_global_config_t)) return;
    cfg_config_cache_core_t *core = NULL;
    if (!cfg_config_cache_nvs_begin(&core)) return;
    cfg_derived_view_t view;
    json_config_fill_view(committed, generation, &view);
    aicam_result_t r = cfg_config_cache_store(core, &view);
    if (r != AICAM_OK)
    {
        LOG_CORE_ERROR("Derived config cache write failed for authoritative generation %u; early boot keeps current marker generation", generation);
        cfg_config_cache_nvs_end();
        return;
    }
    r = cfg_config_cache_marker_store(core, generation);
    cfg_config_cache_nvs_end();
    if (r != AICAM_OK)
    {
        LOG_CORE_ERROR("Authority marker write failed for generation %u; early boot keeps previous committed generation", generation);
    }
}

static aicam_result_t json_config_commit_replace_internal(size_t offset, size_t n, const void *input)
{
    return cfg_txn_commit_replace(&g_json_config_txn, &g_json_config_txn_lock,
                                  &g_json_config_commit_scratch,
                                  sizeof(aicam_global_config_t), offset, n, input,
                                  json_config_persist_blob, NULL,
                                  json_config_derived_post_commit, NULL);
}

static aicam_result_t json_config_commit_replace(size_t offset, size_t n, const void *input)
{
    cfg_gate_state_t st = cfg_writer_gate_begin(&g_json_config_gate);
    if (st != CFG_GATE_READY)
    {
        return (st == CFG_GATE_DEINITIALIZING || st == CFG_GATE_CONTENDED) ? AICAM_ERROR_BUSY : AICAM_ERROR_NOT_INITIALIZED;
    }
    aicam_result_t r = json_config_commit_replace_internal(offset, n, input);
    cfg_writer_gate_end(&g_json_config_gate);
    return r;
}

static aicam_result_t json_config_commit_patch_internal(size_t offset, size_t n,
                                                        cfg_txn_patch_fn patch, void *user)
{
    return cfg_txn_commit_patch(&g_json_config_txn, &g_json_config_txn_lock,
                                &g_json_config_commit_scratch,
                                sizeof(aicam_global_config_t), offset, n,
                                patch, user, json_config_persist_blob, NULL,
                                json_config_derived_post_commit, NULL);
}

static aicam_result_t json_config_commit_patch(size_t offset, size_t n,
                                               cfg_txn_patch_fn patch, void *user)
{
    cfg_gate_state_t st = cfg_writer_gate_begin(&g_json_config_gate);
    if (st != CFG_GATE_READY)
    {
        return (st == CFG_GATE_DEINITIALIZING || st == CFG_GATE_CONTENDED) ? AICAM_ERROR_BUSY : AICAM_ERROR_NOT_INITIALIZED;
    }
    aicam_result_t r = json_config_commit_patch_internal(offset, n, patch, user);
    cfg_writer_gate_end(&g_json_config_gate);
    return r;
}
 
 /* ==================== Default Configuration Definition ==================== */
 
 // Definition of the default configuration
 const aicam_global_config_t default_config = {
     .config_version = JSON_CONFIG_VERSION_CURRENT,
     .magic_number = JSON_CONFIG_MAGIC_NUMBER,
     .checksum = 0,
     .timestamp = 0,
     
     .log_config = {
         .log_level = 2,         // INFO
         .log_file_size_kb = 60,
         .log_file_count = 5
     },
     
     .ai_debug = {
         .ai_enabled = AICAM_FALSE,
         .ai_1_active = AICAM_FALSE,
         .confidence_threshold = 50,
         .nms_threshold = 50,
         .overlay_results = AICAM_TRUE,
         .inference_interval_ms = 0
     },
     
     .power_mode_config = {
         .current_mode = POWER_MODE_LOW_POWER,
         .default_mode = POWER_MODE_LOW_POWER,
         .low_power_timeout_ms = 60000,  // 60 seconds
         .last_activity_time = 0,
         .mode_switch_count = 0
     },
     
    .device_info = {
        .device_name = "AICAM-000000", // Default name, will be updated from MAC
        .mac_address = "00:00:00:00:00:00",
        .serial_number = "SN202500001",
        .hardware_version = "V1.0",
        .software_version = FW_VERSION_STRING,  // From version.h (auto-generated)
        .camera_module = "IMX219 8MP Camera",
         .extension_modules = "-",
         .storage_card_info = "No SD Card",
         .storage_usage_percent = 0.0f,
        .power_supply_type = "External Power",
        .battery_percent = 0.0f,
        .communication_type = "WiFi"
    },
    
    .auth_mgr = {
        .session_timeout_ms = 3600000,  // 1 hour default
        .enable_session_timeout = AICAM_FALSE,  // Default: false
        .admin_password = "hicamthink"
    },
     
     .work_mode_config = {
         .work_mode = AICAM_WORK_MODE_IMAGE,
         .image_mode = {
             .enable = AICAM_TRUE
         },
         .video_stream_mode = {
             .enable = AICAM_FALSE,
             .rtsp_server_url = "rtsp://server.example.com/live",
             .rtsp_enable = AICAM_FALSE,
             .rtsp_port = 554,
             .rtsp_auth_mode = "none",
             .rtsp_username = "",
             .rtsp_password = ""
         },
         .io_trigger = {
             {   // IO trigger 0
                 .pin_number = 0,
                 .enable = AICAM_TRUE,
                 .input_enable = AICAM_TRUE,
                 .output_enable = AICAM_FALSE,
                 .input_trigger_type = AICAM_TRIGGER_TYPE_RISING,
                 .output_trigger_type = AICAM_TRIGGER_TYPE_RISING
             },
             {   // IO trigger 1
                 .pin_number = 1,
                 .enable = AICAM_FALSE,
                 .input_enable = AICAM_FALSE,
                 .output_enable = AICAM_FALSE,
                 .input_trigger_type = AICAM_TRIGGER_TYPE_RISING,
                 .output_trigger_type = AICAM_TRIGGER_TYPE_RISING
             }
         },
         .timer_trigger = {
             .enable = AICAM_FALSE,
             .capture_mode = AICAM_TIMER_CAPTURE_MODE_INTERVAL,
             .interval_sec = 60,
             .time_node_count = 0,
             .time_node = {0},
             .weekdays = {0}
         },
        .pir_trigger = {
            .enable = AICAM_FALSE,
            .pin_number = 2,
            .trigger_type = AICAM_TRIGGER_TYPE_RISING,
            .sensitivity_level = 30,    // Default sensitivity level
            .ignore_time_s = 7,         // Default ignore time (4 seconds)
            .pulse_count = 1,            // Default pulse count (2 pulses)
            .window_time_s = 0,          // Default window time (2 seconds)
            .disable_in_preview = AICAM_TRUE // Default: disable PIR capture during preview
        },
        .remote_trigger = {
            .enable = AICAM_FALSE
        }
    },
     
     .device_service = {
         .image_config = {
             .brightness = 50,
             .contrast = 50,
             .horizontal_flip = AICAM_FALSE,
             .vertical_flip = AICAM_FALSE,
             .aec = 1,  // Auto exposure enabled
             .isp_mode = IMAGE_ISP_MODE_OUTDOOR,
             .grayscale = IMAGE_GRAYSCALE_OFF,
             .startup_skip_frames = 10,  // Default frames to skip for camera stabilization
             .fast_capture_skip_frames = 10,
             .fast_capture_resolution = 0,   // 0: 1280x720
             .fast_capture_jpeg_quality = 60,
             .capture_disable_comm = AICAM_FALSE,
             .capture_storage_ai = AICAM_FALSE
         },
         .light_config = {
             .connected = AICAM_FALSE,
             .mode = LIGHT_MODE_OFF,
             .start_hour = 18,
             .start_minute = 0,
             .end_hour = 6,
             .end_minute = 0,
             .brightness_level = 50,
             .auto_trigger_enabled = AICAM_TRUE,
             .light_threshold = 30,
             .fill_light_while_streaming = AICAM_FALSE
         }
     },
     
     .network_service = {
         .ap_sleep_time = 0,        // Default AP sleep time (0 = no sleep)
         .ssid = "AICAM-AP",        // Default AP SSID
         .password = "",            // Default AP password
         .wifi_country_code = "",   // Default: empty -> firmware default region (US)
         .known_network_count = 0,
         .preferred_comm_type = 0,  // No preferred type
         .enable_auto_priority = AICAM_TRUE,  // Enable auto priority

        // Wi-Fi HaLow last-connected info defaults
        .halow_ssid = "",
        .halow_password = "",
        .halow_security = 0,
        .halow_country_code = "",
        .halow_bssid = "",
        .halow_ip_mode = POE_IP_MODE_DHCP,
        .halow_ip_addr = {192, 168, 12, 199},
        .halow_netmask = {255, 255, 255, 0},
        .halow_gateway = {192, 168, 12, 1},
        .halow_tx_power_dbm = NETIF_WIFI_HALOW_DEFAULT_TX_PWR,
        .halow_scan_dwell_ms = NETIF_WIFI_HALOW_DEFAULT_SCAN_DWELL,
        .halow_rc_mcs = -1,
        .halow_rc_bw_mhz = -1,
        .halow_rc_gi = -1,
        .halow_ps_mode = 0,
        .halow_join_channel = 0,

         // PoE/Ethernet default configuration
         .poe = {
             .ip_mode = POE_IP_MODE_DHCP,                // Default to DHCP
             .ip_addr = {192, 168, 1, 100},              // Default static IP
             .netmask = {255, 255, 255, 0},              // Default netmask
             .gateway = {192, 168, 1, 1},                // Default gateway
             .dns_primary = {8, 8, 8, 8},                // Google DNS
             .dns_secondary = {8, 8, 4, 4},              // Google DNS secondary
             .hostname = "",                             // Default hostname (empty)
             .dhcp_timeout_ms = 30000,                   // 30 seconds DHCP timeout
             .dhcp_retry_count = 3,                      // 3 retries
             .dhcp_retry_interval_ms = 5000,             // 5 seconds between retries
             .power_recovery_delay_ms = 5000,            // 5 seconds recovery delay target
             .auto_reconnect = AICAM_TRUE,               // Auto reconnect enabled
             .persist_last_ip = AICAM_TRUE,              // Persist last DHCP IP
             .last_dhcp_ip = {0, 0, 0, 0},               // No last IP
             .validate_gateway = AICAM_FALSE,            // Don't validate gateway by default
             .detect_ip_conflict = AICAM_FALSE,          // Don't detect conflicts by default
             .last_status = POE_STATUS_OFFLINE,          // Initial status
             .last_error_time = 0,
             .last_error_msg = ""
         }
     },
     
    .mqtt_service = {
        .base_config = {
            // Basic connection
            .protocol_ver = 4,                      // MQTT 3.1.1
            .hostname = "mqtt.example.com",
            .port = 1883,
            .client_id = "AICAM-000000",
            .clean_session = 1,
            .keepalive = 180,
            
            // Authentication
            .username = "",
            .password = "",
            
            // SSL/TLS configuration - CA certificate
            .ca_cert_path = "",
            .ca_cert_data = "",
            .ca_cert_len = 0,
            
            // SSL/TLS configuration - Client certificate
            .client_cert_path = "",
            .client_cert_data = "",
            .client_cert_len = 0,
            
            // SSL/TLS configuration - Client key
            .client_key_path = "",
            .client_key_data = "",
            .client_key_len = 0,
            
            .verify_hostname = 0,
            
            // Last Will and Testament
            .lwt_topic = "aicam/status/offline",
            .lwt_message = "offline",
            .lwt_msg_len = 0,                       // 0 = use strlen
            .lwt_qos = 1,
            .lwt_retain = 1,
            
            // Task parameters
            .task_priority = 32,
            .task_stack_size = 4096,
            
            // Network parameters
            .disable_auto_reconnect = 0,
            .outbox_limit = 10,
            .outbox_resend_interval_ms = 1000,
            .outbox_expired_timeout_ms = 30000,
            .reconnect_interval_ms = 10000,
            .timeout_ms = 3000,
            .buffer_size = 0,
            .tx_buf_size = 1536 * 1024, // 1536KB
            .rx_buf_size = 100 * 1024,  // 100KB
        },
        
        // Topic configuration
        .data_receive_topic = "aicam/data/receive",
        .data_report_topic = "aicam/data/report",
        .status_topic = "aicam/status",
        .command_topic = "aicam/command",
        
        // QoS configuration
        .data_receive_qos = 0,
        .data_report_qos = 0,
        .status_qos = 0,
        .command_qos = 0,
        
        // Auto subscription
        .auto_subscribe_receive = AICAM_TRUE,
        .auto_subscribe_command = AICAM_TRUE,
        
        // Message configuration
        .enable_status_report = AICAM_TRUE,
        .status_report_interval_ms = 60000,
        .enable_heartbeat = AICAM_TRUE,
        .heartbeat_interval_ms = 30000,
        .report_content = MQTT_REPORT_CONTENT_FULL,

        // Continuous AI telemetry
        .telemetry_enabled = AICAM_FALSE,
        .telemetry_topic = "aicam/data/telemetry",
        .telemetry_qos = 0,
        .telemetry_format = MQTT_TELEMETRY_FORMAT_JSON
    },

 };

 /* ==================== Public API Implementation ==================== */

 static void json_config_sync_derived_to_authority(void)
 {
 uint32_t auth_gen = g_json_config_blob.generation;
 if (auth_gen == 0u) return;
 cfg_config_cache_core_t *core = NULL;
 if (!cfg_config_cache_nvs_begin(&core))
 {
 LOG_CORE_ERROR("Derived cache sync skipped: cache io unavailable");
 return;
 }
 uint32_t marker_gen = 0;
 aicam_result_t marker_r = cfg_config_cache_marker_load(core, &marker_gen);
 cfg_derived_view_t probe;
 aicam_bool_t cache_ok = cfg_config_cache_load_for_generation(core, &probe, auth_gen);
 aicam_result_t r = AICAM_OK;
 if (!cache_ok)
 {
 cfg_derived_view_t view;
 json_config_fill_view(&g_json_config_ctx.current_config, auth_gen, &view);
 r = cfg_config_cache_store(core, &view);
 }
 if (r == AICAM_OK && (marker_r != AICAM_OK || marker_gen != auth_gen))
 {
 r = cfg_config_cache_marker_store(core, auth_gen);
 }
 cfg_config_cache_nvs_end();
 if (r != AICAM_OK)
 {
 LOG_CORE_ERROR("Derived cache/marker sync to authoritative generation %u failed: %d", auth_gen, r);
 }
 }

   aicam_result_t json_config_mgr_init(void)
   {
   if (g_json_config_gate.state != CFG_GATE_UNINITIALIZED)
   {
   return AICAM_OK;
   }

   LOG_CORE_INFO("Initializing JSON Config Manager...");

   if (!g_json_config_write_mutex) {
   g_json_config_write_mutex = osMutexNew(NULL);
   if (!g_json_config_write_mutex) {
   LOG_CORE_ERROR("Failed to create json config write mutex");
   return AICAM_ERROR_NO_MEMORY;
   }
   }
   cfg_writer_gate_transition(&g_json_config_gate, CFG_GATE_INITIALIZING);

      cfg_blob_io_t blob_io;
      blob_io.user = NULL;
      blob_io.read = json_config_blob_read;
      blob_io.write = json_config_blob_write;
      cfg_blob_store_init(&g_json_config_blob, &blob_io, sizeof(aicam_global_config_t));

      uint32_t marker_generation = 0;
      aicam_result_t marker_state = AICAM_ERROR_IO;
      {
      cfg_config_cache_core_t *core = NULL;
      if (cfg_config_cache_nvs_begin(&core))
      {
      marker_state = cfg_config_cache_marker_load(core, &marker_generation);
      cfg_config_cache_nvs_end();
      }
      }
      aicam_bool_t marker_present =
      (marker_state == AICAM_OK || marker_state == AICAM_ERROR_IO) ? AICAM_TRUE : AICAM_FALSE;
      cfg_blob_recovery_t policy = cfg_blob_store_recovery_policy(
      cfg_blob_store_loaded(&g_json_config_blob), marker_present);

      aicam_result_t result = AICAM_ERROR_NOT_FOUND;
      if (policy == CFG_BLOB_RECOVERY_USE_AUTHORITATIVE)
      {
      result = cfg_blob_store_load(&g_json_config_blob, &g_json_config_ctx.current_config);
      if (result != AICAM_OK)
      {
      LOG_CORE_ERROR("Config store load failed: %d", result);
      }
      }
      else if (policy == CFG_BLOB_RECOVERY_MIGRATE_LEGACY)
      {
      result = json_config_load_from_nvs(&g_json_config_ctx.current_config);
      if (result != AICAM_OK)
      {
      LOG_CORE_INFO("Failed to load config from NVS, using default: %d", result);
      memcpy(&g_json_config_ctx.current_config, &default_config, sizeof(aicam_global_config_t));
      }
      if (cfg_blob_store_save(&g_json_config_blob, &g_json_config_ctx.current_config) != AICAM_OK)
      {
      LOG_CORE_ERROR("Failed to establish config store, will retry next boot");
      }
      }
      else
      {
      LOG_CORE_ERROR("Config store corrupt after authority established; using defaults");
      memcpy(&g_json_config_ctx.current_config, &default_config, sizeof(aicam_global_config_t));
      if (cfg_blob_store_save(&g_json_config_blob, &g_json_config_ctx.current_config) != AICAM_OK)
      {
      LOG_CORE_ERROR("Failed to re-establish config store, will retry next boot");
      }
      }


      if (strcmp(g_json_config_ctx.current_config.device_info.device_name, "AICAM-000000") == 0 &&
          strcmp(g_json_config_ctx.current_config.device_info.mac_address, "00:00:00:00:00:00") != 0)
      {
          device_info_config_t migrated;
          if (json_config_get_device_info_config(&migrated) == AICAM_OK)
          {
              json_config_generate_device_name_from_mac(migrated.device_name,
                                                        sizeof(migrated.device_name),
                                                        migrated.mac_address);
               aicam_result_t name_result = json_config_commit_replace_internal(
               offsetof(aicam_global_config_t, device_info),
               sizeof(migrated), &migrated);
              if (name_result != AICAM_OK)
              {
                  LOG_CORE_ERROR("Failed to persist generated device name: %d", name_result);
              }
              else
              {
                  LOG_CORE_INFO("Updated device name to: %s", migrated.device_name);
              }
          }
      }

     json_config_sync_derived_to_authority();

     g_json_config_ctx.initialized = AICAM_TRUE;
     g_json_config_ctx.save_count = 0;
     g_json_config_ctx.last_save_time = json_config_get_timestamp();
     cfg_writer_gate_transition(&g_json_config_gate, CFG_GATE_READY);

     LOG_CORE_INFO("JSON Config Manager initialized successfully");
     return AICAM_OK;
 }

  aicam_result_t json_config_mgr_deinit(void)
  {
  if (!g_json_config_write_mutex)
  {
  return AICAM_OK;
  }

  if (!json_config_write_lock())
  {
  return AICAM_ERROR_BUSY;
  }

  if (g_json_config_gate.state != CFG_GATE_READY)
  {
  json_config_write_unlock();
  return AICAM_ERROR_BUSY;
  }

  cfg_writer_gate_transition(&g_json_config_gate, CFG_GATE_DEINITIALIZING);
  memset(&g_json_config_ctx, 0, sizeof(json_config_mgr_context_t));
  cfg_writer_gate_transition(&g_json_config_gate, CFG_GATE_UNINITIALIZED);
  json_config_write_unlock();

  LOG_CORE_INFO("JSON Config Manager deinitialized");
  return AICAM_OK;
  }

 aicam_result_t json_config_load_from_file(const char *file_path, aicam_global_config_t *config)
 {
     // Compatible with original interface, actually load from NVS
     if (!config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     aicam_result_t result = json_config_load_from_nvs(config);
     if (result == AICAM_OK)
     {
         LOG_CORE_INFO("Config loaded from NVS (file interface)");
     }

     return result;
 }


 aicam_result_t json_config_parse_from_string(const char *json_string,
                                              aicam_global_config_t *config,
                                              const json_config_validation_options_t *validation_options)
 {
     if (!json_string || !config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     // Delegate parsing to JSON module
     aicam_result_t result = json_config_parse_json_object(json_string, config);
     if (result != AICAM_OK)
     {
         return result;
     }

     // Validate configuration (if validation options are specified)
     if (validation_options)
     {
         result = json_config_validate(config, validation_options);
         if (result != AICAM_OK)
         {
             return result;
         }
     }

     return AICAM_OK;
 }

 aicam_result_t json_config_serialize_to_string(const aicam_global_config_t *config,
                                                char *json_buffer,
                                                size_t buffer_size)
 {
     if (!config || !json_buffer || buffer_size == 0)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     // Delegate serialization to JSON module
     return json_config_serialize_json_object(config, json_buffer, buffer_size);
 }

 aicam_result_t json_config_load_default(aicam_global_config_t *config)
 {
     if (!config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     memcpy(config, &default_config, sizeof(aicam_global_config_t));
     config->timestamp = json_config_get_timestamp();

     /* Fields not covered by the static default_config table */
     json_config_capture_upload_defaults(&config->capture_upload);

     // Delegate checksum calculation
     aicam_result_t result = json_config_calculate_checksum(config, &config->checksum);
     return result;
 }

 aicam_result_t json_config_validate(const aicam_global_config_t *config,
                                     const json_config_validation_options_t *validation_options)
 {
     if (!config || !validation_options)
     {
         return AICAM_ERROR;
     }

     // Validate magic number
     if (config->magic_number != JSON_CONFIG_MAGIC_NUMBER)
     {
         LOG_CORE_INFO("Invalid magic number: 0x%08X", config->magic_number);
         return AICAM_ERROR;
     }

     // Validate version
     if (config->config_version > JSON_CONFIG_VERSION_CURRENT)
     {
         LOG_CORE_INFO("Unsupported config version: %d", config->config_version);
         return AICAM_ERROR;
     }

     // Validate checksum (if enabled)
     if (validation_options->validate_checksum)
     {
         uint32_t calculated_checksum;
         // Delegate checksum calculation
         aicam_result_t result = json_config_calculate_checksum(config, &calculated_checksum);
         if (result != AICAM_OK)
         {
             return result;
         }

         if (calculated_checksum != config->checksum)
         {
             LOG_CORE_INFO("Checksum mismatch: expected 0x%08X, got 0x%08X",
                           config->checksum, calculated_checksum);
             return AICAM_ERROR;
         }
     }

     // Validate value ranges (if enabled)
     if (validation_options->validate_value_ranges)
     {
         // Delegate range validation
         aicam_result_t result = json_config_validate_ranges(config);
         if (result != AICAM_OK)
         {
             return result;
         }
     }

     return AICAM_OK;
 }

 aicam_result_t json_config_calculate_checksum(const aicam_global_config_t *config, uint32_t *checksum)
 {
     if (!config || !checksum)
     {
         return AICAM_ERROR;
     }

     // Create a temporary configuration, excluding the checksum field
     aicam_global_config_t *temp_config = NULL;

     temp_config = (aicam_global_config_t *)buffer_calloc(1, sizeof(aicam_global_config_t));
     if (!temp_config)
     {
         LOG_CORE_ERROR("Failed to allocate memory for temp config");
         return AICAM_ERROR_NO_MEMORY;
     }

     memcpy(temp_config, config, sizeof(aicam_global_config_t));
     temp_config->checksum = 0;

     // Delegate CRC32 calculation to utils module
     *checksum = json_config_crc32(temp_config, sizeof(aicam_global_config_t));

     buffer_free(temp_config);

     return AICAM_OK;
 }

 aicam_result_t json_config_create_backup(const char *source_path, const char *backup_path)
 {
     // This function was a stub in the original.
     // Re-implementing stub logic.
     aicam_global_config_t *config = NULL;
     aicam_result_t result;

     // Dynamically allocate configuration structure
     config = (aicam_global_config_t *)buffer_calloc(1, sizeof(aicam_global_config_t));
     if (!config)
     {
         LOG_CORE_ERROR("Failed to allocate memory for config");
         return AICAM_ERROR_NO_MEMORY;
     }

     result = json_config_load_from_nvs(config);
     if (result != AICAM_OK)
     {
         buffer_free(config);
         return result;
     }

     // Original logic didn't actually save a backup file, just logged.
     LOG_CORE_INFO("Config backup created in NVS (STUB)");

     // Free memory
     buffer_free(config);

     return AICAM_OK;
 }

 aicam_result_t json_config_restore_from_backup(const char *backup_path, const char *target_path)
 {
     // This function was a stub in the original.
     LOG_CORE_INFO("Config restored from NVS backup (STUB)");
     return AICAM_OK;
 }

 aicam_result_t json_config_reset_to_default(const char *file_path)
 {
     aicam_global_config_t *config = NULL;
     aicam_result_t result;

     // Dynamically allocate configuration structure
     config = (aicam_global_config_t *)buffer_calloc(1, sizeof(aicam_global_config_t));
     if (!config)
     {
         LOG_CORE_ERROR("Failed to allocate memory for config");
         return AICAM_ERROR_NO_MEMORY;
     }

     // Read factory information from NVS_FACTORY partition (preserved across factory reset)
     device_info_config_t preserved_info;
     memset(&preserved_info, 0, sizeof(preserved_info));
     
     // Read from FACTORY partition - this is the source of truth
     storage_nvs_read(NVS_FACTORY, "serial_number", preserved_info.serial_number, 
                      sizeof(preserved_info.serial_number) - 1);
     storage_nvs_read(NVS_FACTORY, "hw_version", preserved_info.hardware_version,
                      sizeof(preserved_info.hardware_version) - 1);
     // MAC is auto-generated by network driver, read from USER if available
     json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_MAC, preserved_info.mac_address,
                                 sizeof(preserved_info.mac_address));

     result = json_config_load_default(config);
     if (result != AICAM_OK)
     {
         buffer_free(config);
         return result;
     }

     // Restore preserved factory information if valid
     if (strlen(preserved_info.serial_number) > 0 && 
         strcmp(preserved_info.serial_number, "SN202500001") != 0)
     {
         strncpy(config->device_info.serial_number, preserved_info.serial_number,
                 sizeof(config->device_info.serial_number) - 1);
     }
     if (strlen(preserved_info.mac_address) > 0 && 
         strcmp(preserved_info.mac_address, "00:00:00:00:00:00") != 0)
     {
         strncpy(config->device_info.mac_address, preserved_info.mac_address,
                 sizeof(config->device_info.mac_address) - 1);
     }
     if (strlen(preserved_info.hardware_version) > 0)
     {
         strncpy(config->device_info.hardware_version, preserved_info.hardware_version,
                 sizeof(config->device_info.hardware_version) - 1);
     }
     else
     {
         /* No factory-written hardware version: use the PE9 board strap band */
         strncpy(config->device_info.hardware_version, board_hw_version_str(),
                 sizeof(config->device_info.hardware_version) - 1);
     }

     LOG_CORE_INFO("Factory info preserved: SN=%s, MAC=%s, HW=%s",
                   config->device_info.serial_number,
                   config->device_info.mac_address,
                   config->device_info.hardware_version);

     result = json_config_set_config(config);
     // Free memory
     buffer_free(config);

     if (result == AICAM_OK) {
         sys_clk_config_t sc = {0};
         if (fsbl_app_write_sys_clk_config(&sc) != 0) {
             LOG_CORE_WARN("Failed to clear persisted boot sys_clk profile during config reset to default");
         }
     }

     return result;
 }

 /* ==================== Specific Get/Set API Implementation ==================== */
 // here we implement the specific get/set API for the configuration manager and set to NVS immediately

 /*=================== Global Configuration API Implementation ====================*/

 aicam_result_t json_config_get_config(aicam_global_config_t *config)
 {
     if (!config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }
     if (!cfg_txn_read(&g_json_config_txn, config, sizeof(*config)))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

aicam_result_t json_config_set_config(aicam_global_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_json_config_write_mutex)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    aicam_global_config_t staging = *config;
    return json_config_commit_replace(0, sizeof(staging), &staging);
    }

 /*=================== Log Configuration API Implementation ====================*/

 aicam_result_t json_config_get_log_config(log_config_t *log_config)
 {
     if (!log_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }
     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, log_config),
                              sizeof(*log_config), log_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_log_config(log_config_t *log_config)
{
    if (!log_config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    log_config_t candidate = *log_config;
    aicam_result_t result = json_config_commit_replace(offsetof(aicam_global_config_t, log_config),
    sizeof(candidate), &candidate);
    if (result != AICAM_OK)
    {
    return result;
    }
    LOG_CORE_INFO("Log configuration updated: level=%d, file_size=%d, file_count=%d",
                  log_config->log_level, log_config->log_file_size_kb, log_config->log_file_count);
    return AICAM_OK;
 }

 /*=================== AI Debug Configuration API Implementation ====================*/

 aicam_bool_t json_config_get_ai_1_active(void)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_FALSE;
     }

     aicam_bool_t active = AICAM_FALSE;
     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, ai_debug.ai_1_active),
                              sizeof(active), &active))
     {
         return AICAM_FALSE;
     }
     return active;
 }

 aicam_result_t json_config_set_ai_1_active(aicam_bool_t ai_1_active)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     aicam_bool_t current = AICAM_FALSE;
     cfg_txn_read_member(&g_json_config_txn,
                         offsetof(aicam_global_config_t, ai_debug.ai_1_active),
                         sizeof(current), &current);
     if (ai_1_active == current)
     {
         return AICAM_OK;
     }

     LOG_CORE_INFO("Update AI_1 active to %d", ai_1_active);
     return json_config_commit_replace(
     offsetof(aicam_global_config_t, ai_debug.ai_1_active),
     sizeof(ai_1_active), &ai_1_active);
 }

 aicam_result_t json_config_sync_ai_pipe_nvs_from_input_size(uint32_t input_width, uint32_t input_height)
 {
     if (input_width == 0U || input_height == 0U)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     uint32_t nvs_w = 0U;
     uint32_t nvs_h = 0U;
     aicam_result_t rw = json_config_nvs_read_uint32(NVS_KEY_AI_PIPE_WIDTH, &nvs_w);
     aicam_result_t rh = json_config_nvs_read_uint32(NVS_KEY_AI_PIPE_HEIGHT, &nvs_h);
     if (rw == AICAM_OK && rh == AICAM_OK && nvs_w == input_width && nvs_h == input_height)
     {
         return AICAM_OK;
     }

     aicam_result_t wret = json_config_nvs_write_uint32(NVS_KEY_AI_PIPE_WIDTH, input_width);
     if (wret != AICAM_OK)
     {
         LOG_CORE_ERROR("Failed to write AI pipe width to NVS: %d", wret);
         return wret;
     }
     wret = json_config_nvs_write_uint32(NVS_KEY_AI_PIPE_HEIGHT, input_height);
     if (wret != AICAM_OK)
     {
         LOG_CORE_ERROR("Failed to write AI pipe height to NVS: %d", wret);
         return wret;
     }

     if (rw == AICAM_OK && rh == AICAM_OK)
     {
         LOG_CORE_INFO("NVS AI pipe size updated from %ux%u to %ux%u",
                       (unsigned)nvs_w, (unsigned)nvs_h,
                       (unsigned)input_width, (unsigned)input_height);
     }
     else
     {
         LOG_CORE_INFO("NVS AI pipe size set to %ux%u", (unsigned)input_width, (unsigned)input_height);
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_confidence_threshold(uint32_t confidence_threshold)
 {
     return json_config_commit_replace(
     offsetof(aicam_global_config_t, ai_debug.confidence_threshold),
     sizeof(confidence_threshold), &confidence_threshold);
 }

 aicam_result_t json_config_set_nms_threshold(uint32_t nms_threshold)
 {
     return json_config_commit_replace(
     offsetof(aicam_global_config_t, ai_debug.nms_threshold),
     sizeof(nms_threshold), &nms_threshold);
 }

 uint32_t json_config_get_confidence_threshold(void)
 {
     uint32_t value = 0;
     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, ai_debug.confidence_threshold),
                              sizeof(value), &value))
     {
         return 0;
     }
     return value;
 }

 uint32_t json_config_get_nms_threshold(void)
 {
     uint32_t value = 0;
     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, ai_debug.nms_threshold),
                              sizeof(value), &value))
     {
         return 0;
     }
     return value;
 }

 aicam_result_t json_config_set_overlay_results(aicam_bool_t overlay_results)
 {
     return json_config_commit_replace(
     offsetof(aicam_global_config_t, ai_debug.overlay_results),
     sizeof(overlay_results), &overlay_results);
 }

 aicam_bool_t json_config_get_overlay_results(void)
 {
     uint32_t value = 0;
     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, ai_debug.overlay_results),
                              sizeof(value), &value))
     {
         return 0;
     }
     return value;
 }

 aicam_result_t json_config_set_inference_interval_ms(uint32_t interval_ms)
 {
     return json_config_commit_replace(
     offsetof(aicam_global_config_t, ai_debug.inference_interval_ms),
     sizeof(interval_ms), &interval_ms);
 }

 uint32_t json_config_get_inference_interval_ms(void)
 {
     uint32_t value = 0;
     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, ai_debug.inference_interval_ms),
                              sizeof(value), &value))
     {
         return 0;
     }
     return value;
 }

 /*=================== Work Mode Configuration API Implementation ====================*/
 aicam_result_t json_config_get_work_mode_config(work_mode_config_t *work_mode_config)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, work_mode_config),
                              sizeof(*work_mode_config), work_mode_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_work_mode_config(work_mode_config_t *work_mode_config)
 {
     if (!work_mode_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     work_mode_config_t candidate = *work_mode_config;
     aicam_result_t result = json_config_commit_replace(
     offsetof(aicam_global_config_t, work_mode_config),
     sizeof(candidate), &candidate);
     if (result != AICAM_OK)
     {
     return result;
     }
     LOG_CORE_INFO("Work mode configuration updated: work_mode=%u, image_mode_enable=%u, video_stream_mode_enable=%u, pir_trigger_enable=%u, pir_trigger_pin_number=%u, pir_trigger_trigger_type=%u, timer_trigger_enable=%u, timer_trigger_capture_mode=%u, timer_trigger_interval=%u",
                   work_mode_config->work_mode, work_mode_config->image_mode.enable, work_mode_config->video_stream_mode.enable, work_mode_config->pir_trigger.enable, work_mode_config->pir_trigger.pin_number, work_mode_config->pir_trigger.trigger_type, work_mode_config->timer_trigger.enable, work_mode_config->timer_trigger.capture_mode, work_mode_config->timer_trigger.interval_sec);

     return result;
 }

 /* ==================== Power Mode Configuration API Implementation ==================== */

 aicam_result_t json_config_get_power_mode_config(power_mode_config_t *config)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, power_mode_config),
                              sizeof(*config), config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_power_mode_config(const power_mode_config_t *config)
 {
     if (!config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     // Validate configuration
     if (config->current_mode >= POWER_MODE_MAX || config->default_mode >= POWER_MODE_MAX)
     {
         LOG_CORE_ERROR("Invalid power mode values: current=%u, default=%u",
                        config->current_mode, config->default_mode);
         return AICAM_ERROR_INVALID_PARAM;
     }

     power_mode_config_t candidate = *config;
     aicam_result_t result = json_config_commit_replace(
     offsetof(aicam_global_config_t, power_mode_config),
     sizeof(candidate), &candidate);
     if (result != AICAM_OK)
     {
     return result;
     }

     LOG_CORE_INFO("Power mode configuration updated: current=%u, default=%u, timeout=%u",
                   config->current_mode, config->default_mode, config->low_power_timeout_ms);

     return AICAM_OK;
 }

 /*=================== Device Info Configuration API Implementation ====================*/
 aicam_result_t json_config_get_device_info_config(device_info_config_t *device_info_config)
 {
     if (!device_info_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }
     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, device_info),
                              sizeof(*device_info_config), device_info_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_device_info_config(device_info_config_t *device_info_config)
 {
     if (!device_info_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     device_info_config_t candidate = *device_info_config;
     return json_config_commit_replace(offsetof(aicam_global_config_t, device_info),
     sizeof(candidate), &candidate);
 }

 aicam_result_t json_config_update_device_mac_address(const char *mac_address)
 {
     if (!mac_address)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     return json_config_commit_patch(offsetof(aicam_global_config_t, device_info),
     sizeof(device_info_config_t),
     json_config_device_info_mac_patch,
     (void *)mac_address);
 }

 aicam_result_t json_config_get_device_password(char *password_buffer, size_t buffer_size)
 {
     if (!password_buffer || buffer_size == 0)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     auth_mgr_config_t auth;
     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, auth_mgr),
                              sizeof(auth), &auth))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     strncpy(password_buffer, auth.admin_password, buffer_size);
     password_buffer[buffer_size - 1] = '\0';

     return AICAM_OK;
 }

 aicam_result_t json_config_set_device_password(const char *password)
 {
     if (!password)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

    // Validate password length
    size_t password_len = strlen(password);
    if (password_len == 0 || password_len >= sizeof(g_json_config_ctx.current_config.auth_mgr.admin_password))
    {
        LOG_CORE_ERROR("Invalid password length: %zu (must be 1-%zu characters)",
                       password_len, sizeof(g_json_config_ctx.current_config.auth_mgr.admin_password) - 1);
        return AICAM_ERROR_INVALID_PARAM;
    }

    aicam_result_t result = json_config_commit_patch(offsetof(aicam_global_config_t, auth_mgr),
    sizeof(auth_mgr_config_t),
    json_config_password_patch,
    (void *)password);
    if (result != AICAM_OK)
    {
    return result;
    }
    LOG_CORE_INFO("Device admin password updated successfully");
    return AICAM_OK;
 }

 /*=================== Device Service Configuration API Implementation ====================*/
 aicam_result_t json_config_get_device_service_image_config(image_config_t *image_config)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, device_service.image_config),
                              sizeof(*image_config), image_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_device_service_image_config(const image_config_t *image_config)
 {
    if (!image_config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (image_config->isp_mode != IMAGE_ISP_MODE_INDOOR &&
        image_config->isp_mode != IMAGE_ISP_MODE_OUTDOOR &&
        image_config->isp_mode != IMAGE_ISP_MODE_CUSTOM)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    image_config_t candidate = *image_config;
    aicam_result_t result = json_config_commit_replace(
    offsetof(aicam_global_config_t, device_service.image_config),
    sizeof(candidate), &candidate);
    if (result != AICAM_OK) {
    return result;
    }
    LOG_CORE_INFO("Device service image configuration updated: brightness=%u, contrast=%u, horizontal_flip=%u, vertical_flip=%u",
                   image_config->brightness, image_config->contrast, image_config->horizontal_flip, image_config->vertical_flip);
     return AICAM_OK;
 }

 aicam_result_t json_config_get_device_service_light_config(light_config_t *light_config)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     if (!cfg_txn_read_member(&g_json_config_txn,
                              offsetof(aicam_global_config_t, device_service.light_config),
                              sizeof(*light_config), light_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_device_service_light_config(const light_config_t *light_config)
 {
     if (!light_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     light_config_t candidate = *light_config;
     aicam_result_t result = json_config_commit_replace(
     offsetof(aicam_global_config_t, device_service.light_config),
     sizeof(candidate), &candidate);
     if (result != AICAM_OK) {
     return result;
     }
     LOG_CORE_INFO("Device service light configuration updated: connected=%u, mode=%u, start_hour=%u, start_minute=%u, end_hour=%u, end_minute=%u, brightness_level=%u, auto_trigger_enabled=%u, light_threshold=%u, fill_light_while_streaming=%u",
                   light_config->connected, light_config->mode, light_config->start_hour, light_config->start_minute, light_config->end_hour, light_config->end_minute, light_config->brightness_level, light_config->auto_trigger_enabled, light_config->light_threshold, light_config->fill_light_while_streaming);
     return AICAM_OK;
 }

/*=================== ISP Configuration API Implementation ====================*/
aicam_result_t json_config_get_isp_config(isp_config_t *isp_config)
{
    if (!isp_config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    if (!cfg_txn_read_member(&g_json_config_txn,
                             offsetof(aicam_global_config_t, device_service.isp_config),
                             sizeof(*isp_config), isp_config))
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return AICAM_OK;
}

aicam_result_t json_config_set_isp_config(const isp_config_t *isp_config)
{
    if (!isp_config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    isp_config_t candidate = *isp_config;
    aicam_result_t result = json_config_commit_replace(
    offsetof(aicam_global_config_t, device_service.isp_config),
    sizeof(candidate), &candidate);
    if (result != AICAM_OK) {
    return result;
    }
    LOG_CORE_INFO("ISP configuration saved: valid=%u, aec_en=%u, awb_en=%u, gamma_en=%u",
                  isp_config->valid, isp_config->aec_enable, isp_config->awb_enable, isp_config->gamma_enable);
    return AICAM_OK;
}

aicam_result_t json_config_isp_param_to_config(ISP_IQParamTypeDef *isp_param, isp_config_t *isp_config)
{
    if (isp_param == NULL || isp_config == NULL)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    memset(isp_config, 0, sizeof(isp_config_t));
    isp_config->valid = AICAM_TRUE;

    /* StatRemoval */
    isp_config->stat_removal_enable      = isp_param->statRemoval.enable;
    isp_config->stat_removal_head_lines  = isp_param->statRemoval.nbHeadLines;
    isp_config->stat_removal_valid_lines = isp_param->statRemoval.nbValidLines;

    /* Demosaicing */
    isp_config->demosaic_enable = isp_param->demosaicing.enable;
    isp_config->demosaic_type   = (uint8_t)isp_param->demosaicing.type;
    isp_config->demosaic_peak   = isp_param->demosaicing.peak;
    isp_config->demosaic_line_v = isp_param->demosaicing.lineV;
    isp_config->demosaic_line_h = isp_param->demosaicing.lineH;
    isp_config->demosaic_edge   = isp_param->demosaicing.edge;

    /* Contrast */
    isp_config->contrast_enable    = isp_param->contrast.enable;
    isp_config->contrast_lut[0]    = isp_param->contrast.coeff.LUM_0;
    isp_config->contrast_lut[1]    = isp_param->contrast.coeff.LUM_32;
    isp_config->contrast_lut[2]    = isp_param->contrast.coeff.LUM_64;
    isp_config->contrast_lut[3]    = isp_param->contrast.coeff.LUM_96;
    isp_config->contrast_lut[4]    = isp_param->contrast.coeff.LUM_128;
    isp_config->contrast_lut[5]    = isp_param->contrast.coeff.LUM_160;
    isp_config->contrast_lut[6]    = isp_param->contrast.coeff.LUM_192;
    isp_config->contrast_lut[7]    = isp_param->contrast.coeff.LUM_224;
    isp_config->contrast_lut[8]    = isp_param->contrast.coeff.LUM_256;

    /* Stat area */
    isp_config->stat_area_x      = isp_param->statAreaStatic.X0;
    isp_config->stat_area_y      = isp_param->statAreaStatic.Y0;
    isp_config->stat_area_width  = isp_param->statAreaStatic.XSize;
    isp_config->stat_area_height = isp_param->statAreaStatic.YSize;

    /* Sensor gain / exposure (static) */
    isp_config->sensor_gain     = isp_param->sensorGainStatic.gain;
    isp_config->sensor_exposure = isp_param->sensorExposureStatic.exposure;

    /* BadPixel algo */
    isp_config->bad_pixel_algo_enable   = isp_param->badPixelAlgo.enable;
    isp_config->bad_pixel_algo_threshold = isp_param->badPixelAlgo.threshold;

    /* BadPixel static */
    isp_config->bad_pixel_enable  = isp_param->badPixelStatic.enable;
    isp_config->bad_pixel_strength = isp_param->badPixelStatic.strength;

    /* Black level */
    isp_config->black_level_enable = isp_param->blackLevelStatic.enable;
    isp_config->black_level_r      = isp_param->blackLevelStatic.BLCR;
    isp_config->black_level_g      = isp_param->blackLevelStatic.BLCG;
    isp_config->black_level_b      = isp_param->blackLevelStatic.BLCB;

    /* AEC algo */
    isp_config->aec_enable                = isp_param->AECAlgo.enable;
    isp_config->aec_exposure_compensation = isp_param->AECAlgo.exposureCompensation;
    isp_config->aec_anti_flicker_freq     = isp_param->AECAlgo.antiFlickerFreq;

    /* AWB algo (5 profiles) */
    isp_config->awb_enable = isp_param->AWBAlgo.enable;
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++)
    {
        memcpy(isp_config->awb_label[i], isp_param->AWBAlgo.label[i], ISP_AWB_LABEL_MAX_LEN);
        isp_config->awb_ref_color_temp[i] = isp_param->AWBAlgo.referenceColorTemp[i];
        isp_config->awb_gain_r[i]         = isp_param->AWBAlgo.ispGainR[i];
        isp_config->awb_gain_g[i]         = isp_param->AWBAlgo.ispGainG[i];
        isp_config->awb_gain_b[i]         = isp_param->AWBAlgo.ispGainB[i];
        memcpy(isp_config->awb_ccm[i], isp_param->AWBAlgo.coeff[i], sizeof(isp_config->awb_ccm[i]));
        memcpy(isp_config->awb_ref_rgb[i], isp_param->AWBAlgo.referenceRGB[i], sizeof(isp_config->awb_ref_rgb[i]));
    }

    /* ISP gain (static) */
    isp_config->isp_gain_enable = isp_param->ispGainStatic.enable;
    isp_config->isp_gain_r      = isp_param->ispGainStatic.ispGainR;
    isp_config->isp_gain_g      = isp_param->ispGainStatic.ispGainG;
    isp_config->isp_gain_b      = isp_param->ispGainStatic.ispGainB;

    /* Color conversion (static) */
    isp_config->color_conv_enable = isp_param->colorConvStatic.enable;
    memcpy(isp_config->color_conv_matrix, isp_param->colorConvStatic.coeff, sizeof(isp_config->color_conv_matrix));

    /* Gamma */
    isp_config->gamma_enable = isp_param->gamma.enable;

    /* Sensor delay */
    isp_config->sensor_delay = isp_param->sensorDelay.delay;

    /* Lux reference */
    isp_config->lux_hl_ref      = isp_param->luxRef.HL_LuxRef;
    isp_config->lux_hl_expo1    = isp_param->luxRef.HL_Expo1;
    isp_config->lux_hl_lum1     = isp_param->luxRef.HL_Lum1;
    isp_config->lux_hl_expo2    = isp_param->luxRef.HL_Expo2;
    isp_config->lux_hl_lum2     = isp_param->luxRef.HL_Lum2;
    isp_config->lux_ll_ref      = isp_param->luxRef.LL_LuxRef;
    isp_config->lux_ll_expo1    = isp_param->luxRef.LL_Expo1;
    isp_config->lux_ll_lum1     = isp_param->luxRef.LL_Lum1;
    isp_config->lux_ll_expo2    = isp_param->luxRef.LL_Expo2;
    isp_config->lux_ll_lum2     = isp_param->luxRef.LL_Lum2;
    isp_config->lux_calib_factor = isp_param->luxRef.calibFactor;

    return AICAM_OK;
}

aicam_result_t json_config_config_to_isp_param(isp_config_t *isp_config, ISP_IQParamTypeDef *isp_param)
{
    if (isp_config == NULL || isp_param == NULL)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    memset(isp_param, 0, sizeof(ISP_IQParamTypeDef));

    /* StatRemoval */
    isp_param->statRemoval.enable      = isp_config->stat_removal_enable;
    isp_param->statRemoval.nbHeadLines = isp_config->stat_removal_head_lines;
    isp_param->statRemoval.nbValidLines = isp_config->stat_removal_valid_lines;

    /* Demosaicing */
    isp_param->demosaicing.enable = isp_config->demosaic_enable;
    isp_param->demosaicing.type   = (ISP_DemosTypeTypeDef)isp_config->demosaic_type;
    isp_param->demosaicing.peak   = isp_config->demosaic_peak;
    isp_param->demosaicing.lineV  = isp_config->demosaic_line_v;
    isp_param->demosaicing.lineH  = isp_config->demosaic_line_h;
    isp_param->demosaicing.edge   = isp_config->demosaic_edge;

    /* Contrast */
    isp_param->contrast.enable           = isp_config->contrast_enable;
    isp_param->contrast.coeff.LUM_0      = isp_config->contrast_lut[0];
    isp_param->contrast.coeff.LUM_32     = isp_config->contrast_lut[1];
    isp_param->contrast.coeff.LUM_64     = isp_config->contrast_lut[2];
    isp_param->contrast.coeff.LUM_96     = isp_config->contrast_lut[3];
    isp_param->contrast.coeff.LUM_128    = isp_config->contrast_lut[4];
    isp_param->contrast.coeff.LUM_160    = isp_config->contrast_lut[5];
    isp_param->contrast.coeff.LUM_192    = isp_config->contrast_lut[6];
    isp_param->contrast.coeff.LUM_224    = isp_config->contrast_lut[7];
    isp_param->contrast.coeff.LUM_256    = isp_config->contrast_lut[8];

    /* Stat area */
    isp_param->statAreaStatic.X0    = isp_config->stat_area_x;
    isp_param->statAreaStatic.Y0    = isp_config->stat_area_y;
    isp_param->statAreaStatic.XSize = isp_config->stat_area_width;
    isp_param->statAreaStatic.YSize = isp_config->stat_area_height;

    /* Sensor gain / exposure (static) */
    isp_param->sensorGainStatic.gain        = isp_config->sensor_gain;
    isp_param->sensorExposureStatic.exposure = isp_config->sensor_exposure;

    /* BadPixel algo */
    isp_param->badPixelAlgo.enable    = isp_config->bad_pixel_algo_enable;
    isp_param->badPixelAlgo.threshold = isp_config->bad_pixel_algo_threshold;

    /* BadPixel static */
    isp_param->badPixelStatic.enable  = isp_config->bad_pixel_enable;
    isp_param->badPixelStatic.strength = isp_config->bad_pixel_strength;

    /* Black level */
    isp_param->blackLevelStatic.enable = isp_config->black_level_enable;
    isp_param->blackLevelStatic.BLCR   = isp_config->black_level_r;
    isp_param->blackLevelStatic.BLCG   = isp_config->black_level_g;
    isp_param->blackLevelStatic.BLCB   = isp_config->black_level_b;

    /* AEC algo */
    isp_param->AECAlgo.enable                = isp_config->aec_enable;
    isp_param->AECAlgo.exposureCompensation  = isp_config->aec_exposure_compensation;
    isp_param->AECAlgo.antiFlickerFreq       = isp_config->aec_anti_flicker_freq;

    /* AWB algo */
    isp_param->AWBAlgo.enable = isp_config->awb_enable;
    for (int i = 0; i < ISP_AWB_PROFILES_MAX; i++)
    {
        memcpy(isp_param->AWBAlgo.label[i], isp_config->awb_label[i], ISP_AWB_LABEL_MAX_LEN);
        isp_param->AWBAlgo.referenceColorTemp[i] = isp_config->awb_ref_color_temp[i];
        isp_param->AWBAlgo.ispGainR[i]           = isp_config->awb_gain_r[i];
        isp_param->AWBAlgo.ispGainG[i]           = isp_config->awb_gain_g[i];
        isp_param->AWBAlgo.ispGainB[i]           = isp_config->awb_gain_b[i];
        memcpy(isp_param->AWBAlgo.coeff[i], isp_config->awb_ccm[i], sizeof(isp_param->AWBAlgo.coeff[i]));
        memcpy(isp_param->AWBAlgo.referenceRGB[i], isp_config->awb_ref_rgb[i], sizeof(isp_param->AWBAlgo.referenceRGB[i]));
    }

    /* ISP gain (static) */
    isp_param->ispGainStatic.enable  = isp_config->isp_gain_enable;
    isp_param->ispGainStatic.ispGainR = isp_config->isp_gain_r;
    isp_param->ispGainStatic.ispGainG = isp_config->isp_gain_g;
    isp_param->ispGainStatic.ispGainB = isp_config->isp_gain_b;

    /* Color conversion (static) */
    isp_param->colorConvStatic.enable = isp_config->color_conv_enable;
    memcpy(isp_param->colorConvStatic.coeff, isp_config->color_conv_matrix, sizeof(isp_param->colorConvStatic.coeff));

    /* Gamma */
    isp_param->gamma.enable = isp_config->gamma_enable;

    /* Sensor delay */
    isp_param->sensorDelay.delay = isp_config->sensor_delay;

    /* Lux reference */
    isp_param->luxRef.HL_LuxRef    = isp_config->lux_hl_ref;
    isp_param->luxRef.HL_Expo1     = isp_config->lux_hl_expo1;
    isp_param->luxRef.HL_Lum1      = isp_config->lux_hl_lum1;
    isp_param->luxRef.HL_Expo2     = isp_config->lux_hl_expo2;
    isp_param->luxRef.HL_Lum2      = isp_config->lux_hl_lum2;
    isp_param->luxRef.LL_LuxRef    = isp_config->lux_ll_ref;
    isp_param->luxRef.LL_Expo1     = isp_config->lux_ll_expo1;
    isp_param->luxRef.LL_Lum1      = isp_config->lux_ll_lum1;
    isp_param->luxRef.LL_Expo2     = isp_config->lux_ll_expo2;
    isp_param->luxRef.LL_Lum2      = isp_config->lux_ll_lum2;
    isp_param->luxRef.calibFactor  = isp_config->lux_calib_factor;

    return AICAM_OK;
}

 /*=================== Network Service Configuration API Implementation ====================*/
 aicam_result_t json_config_get_network_service_config(network_service_config_t *network_service_config)
 {
     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, network_service),
                              sizeof(*network_service_config), network_service_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_network_service_config(network_service_config_t *network_service_config)
 {
     if (!network_service_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     network_service_config_t candidate = *network_service_config;
     aicam_result_t result = json_config_commit_replace(
     offsetof(aicam_global_config_t, network_service),
     sizeof(candidate), &candidate);
     if (result != AICAM_OK)
     {
     return result;
     }
     LOG_CORE_INFO("Network service configuration updated: SSID=%s, Sleep=%d",
                   network_service_config->ssid, network_service_config->ap_sleep_time);
    return AICAM_OK;
 }

 /*=================== MQTT Service Configuration API Implementation ====================*/

 aicam_result_t json_config_get_mqtt_service_config(mqtt_service_config_t *mqtt_service_config)
 {

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, mqtt_service),
                              sizeof(*mqtt_service_config), mqtt_service_config))
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }
     return AICAM_OK;
 }

 aicam_result_t json_config_set_mqtt_service_config(const mqtt_service_config_t *mqtt_service_config)
 {
     if (!mqtt_service_config)
     {
         return AICAM_ERROR_INVALID_PARAM;
     }

     if (!g_json_config_ctx.initialized)
     {
         return AICAM_ERROR_NOT_INITIALIZED;
     }

     mqtt_service_config_t candidate = *mqtt_service_config;
     return json_config_commit_replace(
     offsetof(aicam_global_config_t, mqtt_service),
     sizeof(candidate), &candidate);
 }

/*=================== PoE Configuration API Implementation ====================*/

aicam_result_t json_config_get_poe_config(poe_config_persist_t *poe_config)
{
    if (!poe_config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, network_service.poe),
                             sizeof(*poe_config), poe_config))
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return AICAM_OK;
}

aicam_result_t json_config_set_poe_config(const poe_config_persist_t *poe_config)
{
    if (!poe_config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    poe_config_persist_t candidate = *poe_config;
    aicam_result_t result = json_config_commit_replace(
    offsetof(aicam_global_config_t, network_service.poe),
    sizeof(candidate), &candidate);
    if (result != AICAM_OK)
    {
    return result;
    }
    LOG_CORE_INFO("PoE configuration updated: mode=%d, ip=%d.%d.%d.%d",
                  poe_config->ip_mode,
                  poe_config->ip_addr[0], poe_config->ip_addr[1],
                  poe_config->ip_addr[2], poe_config->ip_addr[3]);
    return AICAM_OK;
}

poe_ip_mode_t json_config_get_poe_ip_mode(void)
{
    if (!g_json_config_ctx.initialized)
    {
        return POE_IP_MODE_DHCP;  // Default to DHCP
    }
    poe_ip_mode_t mode = POE_IP_MODE_DHCP;
    if (!cfg_txn_read_member(&g_json_config_txn,
                             offsetof(aicam_global_config_t, network_service.poe.ip_mode),
                             sizeof(mode), &mode))
    {
        return POE_IP_MODE_DHCP;
    }
    return mode;
}

static void json_config_poe_ip_mode_patch(void *member, size_t n, void *user)
{
    (void)n;
    ((poe_config_persist_t *)member)->ip_mode = *(poe_ip_mode_t *)user;
}

aicam_result_t json_config_set_poe_ip_mode(poe_ip_mode_t mode)
{
    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    return json_config_commit_patch(
    offsetof(aicam_global_config_t, network_service.poe),
    sizeof(poe_config_persist_t),
    json_config_poe_ip_mode_patch, &mode);
}

aicam_result_t json_config_save_poe_last_dhcp_ip(const uint8_t *ip_addr)
{
    if (!ip_addr)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    return json_config_commit_replace(
    offsetof(aicam_global_config_t, network_service.poe.last_dhcp_ip),
    4, ip_addr);
}

aicam_result_t json_config_save_halow_join_channel(uint8_t channel)
{
    if (!g_json_config_ctx.initialized)
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }

    return json_config_commit_replace(
    offsetof(aicam_global_config_t, network_service.halow_join_channel),
    sizeof(channel), &channel);
}

const char* poe_status_code_to_string(poe_status_code_t status)
{
    switch (status) {
        case POE_STATUS_OFFLINE:            return "offline";
        case POE_STATUS_LINK_DOWN:          return "link_down";
        case POE_STATUS_CONNECTING:         return "connecting";
        case POE_STATUS_CONNECTED:          return "connected";
        case POE_STATUS_DHCP_FAILED:        return "dhcp_failed";
        case POE_STATUS_STATIC_CONFIG_ERROR: return "static_config_error";
        case POE_STATUS_IP_CONFLICT:        return "ip_conflict";
        case POE_STATUS_GATEWAY_UNREACHABLE: return "gateway_unreachable";
        case POE_STATUS_DNS_ERROR:          return "dns_error";
        case POE_STATUS_ERROR:              return "error";
        default:                            return "unknown";
    }
}

/*=================== Video Stream Mode Configuration API Implementation ====================*/

aicam_result_t json_config_get_video_stream_mode(video_stream_mode_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    if (!cfg_txn_read_member(&g_json_config_txn,
                             offsetof(aicam_global_config_t,
                                      work_mode_config.video_stream_mode),
                             sizeof(*config), config))
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return AICAM_OK;
}

aicam_result_t json_config_set_video_stream_mode(const video_stream_mode_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;

    video_stream_mode_config_t candidate = *config;
    return json_config_commit_replace(
    offsetof(aicam_global_config_t, work_mode_config.video_stream_mode),
    sizeof(candidate), &candidate);
}

aicam_result_t json_config_get_webhook_config(webhook_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, webhook_config),
                             sizeof(*config), config))
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return AICAM_OK;
}

aicam_result_t json_config_set_webhook_config(const webhook_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    webhook_config_t candidate = *config;
    return json_config_commit_replace(
    offsetof(aicam_global_config_t, webhook_config),
    sizeof(candidate), &candidate);
}

aicam_result_t json_config_get_line_counting_config(line_counting_config_t *config) {
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    if (!g_json_config_ctx.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, line_counting),
                             sizeof(*config), config))
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return AICAM_OK;
}

aicam_result_t json_config_set_line_counting_config(const line_counting_config_t *config) {
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    if (!g_json_config_ctx.initialized) return AICAM_ERROR_NOT_INITIALIZED;
    return json_config_commit_replace(offsetof(aicam_global_config_t, line_counting),
    sizeof(*config), config);
}

/*=================== RO Snapshot API Implementation ====================*/

/* ==================== Capture-Upload Configuration ==================== */

void json_config_capture_upload_defaults(capture_upload_config_t *config)
{
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->version              = CAPTURE_UPLOAD_CFG_VERSION;
    config->mode                 = CAPTURE_MODE_INSTANT;
    config->storage              = CAPTURE_STORE_AUTO;
    config->policy               = STORAGE_POLICY_WRAP;
    config->upload_protocol      = UPLOAD_PROTO_MQTT;
    config->retry_enable         = AICAM_TRUE;
    config->retry_max_attempts   = 5;
    config->batch_count          = 10;
    config->schedule_node_count  = 0;
    config->keep_sent_hours      = CAPUP_KEEP_SENT_MAX_HOURS;  /* keep forever; delete only on full/count cap */
    config->max_pending_records  = 200;
    config->flash_max_records    = CAPUP_FLASH_RECORDS_DEFAULT; /* total cap across all states */
    config->upload_comm_type     = 0;  /* COMM_TYPE_NONE = default logic */
}

aicam_result_t json_config_get_capture_upload_config(capture_upload_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    if (!cfg_txn_read_member(&g_json_config_txn, offsetof(aicam_global_config_t, capture_upload),
                             sizeof(*config), config))
    {
        return AICAM_ERROR_NOT_INITIALIZED;
    }
    return AICAM_OK;
}

aicam_result_t json_config_set_capture_upload_config(const capture_upload_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;

    /* Light normalization before persisting so callers don't have to. */
    capture_upload_config_t norm = *config;
    if (norm.version == 0) norm.version = CAPTURE_UPLOAD_CFG_VERSION;
    if (norm.mode    >= CAPTURE_MODE_LOCAL_ONLY + 1) norm.mode    = CAPTURE_MODE_INSTANT;
    if (norm.storage >  CAPTURE_STORE_NONE)          norm.storage = CAPTURE_STORE_AUTO;
    if (norm.policy  >  STORAGE_POLICY_STOP)         norm.policy  = STORAGE_POLICY_WRAP;
    if (norm.upload_protocol > UPLOAD_PROTO_WEBHOOK) norm.upload_protocol = UPLOAD_PROTO_MQTT;
    /* retry_max_attempts: 0 = unlimited, 1..20 otherwise */
    if (norm.retry_max_attempts > 20) norm.retry_max_attempts = 20;
    /* batch_count: 2..20 (1 makes no sense for "batch") */
    if (norm.batch_count < 2)  norm.batch_count = 2;
    if (norm.batch_count > 20) norm.batch_count = 20;
    if (norm.schedule_node_count > CAPTURE_SCHEDULE_MAX_NODES)
        norm.schedule_node_count = CAPTURE_SCHEDULE_MAX_NODES;
    for (uint8_t i = 0; i < CAPTURE_SCHEDULE_MAX_NODES; i++) {
        if (norm.schedule_minutes[i] > 1439) norm.schedule_minutes[i] = 0;
    }
    if (norm.keep_sent_hours > CAPUP_KEEP_SENT_MAX_HOURS)
        norm.keep_sent_hours = CAPUP_KEEP_SENT_MAX_HOURS;
    if (norm.max_pending_records == 0)  norm.max_pending_records = 200;
    if (norm.max_pending_records > 1000) norm.max_pending_records = 1000;
    /* flash_max_records: 0 or out-of-range = default; floor at min */
    if (norm.flash_max_records == 0 ||
        norm.flash_max_records > CAPUP_FLASH_RECORDS_MAX) {
        norm.flash_max_records = CAPUP_FLASH_RECORDS_DEFAULT;
    }
    if (norm.flash_max_records < CAPUP_FLASH_RECORDS_MIN) {
        norm.flash_max_records = CAPUP_FLASH_RECORDS_MIN;
    }

    /* Cross-field constraints */
    if (norm.storage == CAPTURE_STORE_NONE && norm.mode != CAPTURE_MODE_INSTANT) {
        /* "none" only allowed with INSTANT; downgrade to AUTO. */
        norm.storage = CAPTURE_STORE_AUTO;
    }
    if (norm.mode == CAPTURE_MODE_LOCAL_ONLY) {
        norm.retry_enable = AICAM_FALSE;
    }
    if (norm.storage == CAPTURE_STORE_NONE) {
        norm.retry_enable = AICAM_FALSE;
    }

    return json_config_commit_replace(
    offsetof(aicam_global_config_t, capture_upload),
    sizeof(norm), &norm);
}
