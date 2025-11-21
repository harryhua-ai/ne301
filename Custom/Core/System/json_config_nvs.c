/**
 * @file json_config_nvs.c
 * @brief AI Camera JSON Configuration NVS Storage Implementation
 * @details This file handles the saving and loading of configuration
 * values to and from the NVS (Non-Volatile Storage) system.
 */

#include "json_config_internal.h"

/* ==================== NVS Storage Implementation ==================== */

// save log configuration to NVS
aicam_result_t json_config_save_log_config_to_nvs(const log_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_uint8(NVS_KEY_LOG_LEVEL, config->log_level);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save log level to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LOG_FILE_SIZE, config->log_file_size_kb);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save log file size to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LOG_FILE_COUNT, config->log_file_count);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save log file count to NVS");

    LOG_CORE_INFO("Log configuration saved to NVS successfully");
    return result;
}

// save ai debug configuration to NVS
aicam_result_t json_config_save_ai_debug_config_to_nvs(const ai_debug_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_bool(NVS_KEY_AI_ENABLE, config->ai_enabled);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save ai enable to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_AI_1_ACTIVE, config->ai_1_active);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save ai_1_active to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_CONFIDENCE, config->confidence_threshold);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save confidence threshold to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_NMS_THRESHOLD, config->nms_threshold);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save nms threshold to NVS");

    LOG_CORE_INFO("AI debug configuration saved to NVS successfully");
    return result;
}

// save work mode configuration to NVS
aicam_result_t json_config_save_work_mode_config_to_nvs(const work_mode_config_t *config)
{
    if (!config)
    {
        LOG_CORE_ERROR("Invalid work mode configuration");
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_uint32(NVS_KEY_WORK_MODE, (uint32_t)config->work_mode);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save work mode to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_IMAGE_MODE_ENABLE, config->image_mode.enable);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image mode enable to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_VIDEO_STREAM_MODE_ENABLE, config->video_stream_mode.enable);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save video stream mode enable to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_PIR_ENABLE, config->pir_trigger.enable);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save pir trigger enable to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_PIR_PIN, config->pir_trigger.pin_number);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save pir pin to NVS");
    result = json_config_nvs_write_uint8(NVS_KEY_PIR_TRIGGER_TYPE, config->pir_trigger.trigger_type);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save pir trigger type to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_TIMER_ENABLE, config->timer_trigger.enable);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save timer capture enable to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_TIMER_CAPTURE_MODE, config->timer_trigger.capture_mode);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save timer capture mode to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_TIMER_INTERVAL, config->timer_trigger.interval_sec);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save timer interval to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_TIMER_NODE_COUNT, config->timer_trigger.time_node_count);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save timer node count to NVS");

    // Save time nodes array
    for (int i = 0; i < config->timer_trigger.time_node_count; i++)
    {
        char key_name[32];
        snprintf(key_name, sizeof(key_name), "%s%d", NVS_KEY_TIMER_NODE_PREFIX, i);
        result = json_config_nvs_write_uint32(key_name, config->timer_trigger.time_node[i]);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save timer node %d to NVS", i);
    }

    // Save weekdays array
    for (int i = 0; i < config->timer_trigger.time_node_count; i++)
    {
        char key_name[32];
        snprintf(key_name, sizeof(key_name), "%s%d", NVS_KEY_TIMER_WEEKDAYS_PREFIX, i);
        result = json_config_nvs_write_uint8(key_name, config->timer_trigger.weekdays[i]);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save timer weekday %d to NVS", i);
    }

    result = json_config_nvs_write_string(NVS_KEY_RTSP_URL, config->video_stream_mode.rtsp_server_url);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save rtsp url to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_REMOTE_TRIGGER_ENABLE, config->remote_trigger.enable);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save remote trigger enable to NVS");

    // Save IO trigger configuration (array of IO_TRIGGER_MAX triggers)
    for (int i = 0; i < IO_TRIGGER_MAX; i++)
    {
        char key_suffix[16];
        snprintf(key_suffix, sizeof(key_suffix), "_%d", i); // e.g., "_0"

        char key_name[32];

        // Use prefix from internal.h (e.g., "io_enable")
        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_ENABLE_PREFIX, key_suffix);
        result = json_config_nvs_write_bool(key_name, config->io_trigger[i].enable);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save io trigger %d enable to NVS", i);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_PIN_PREFIX, key_suffix);
        result = json_config_nvs_write_uint8(key_name, config->io_trigger[i].pin_number);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save io trigger %d pin to NVS", i);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_INPUT_EN_PREFIX, key_suffix);
        result = json_config_nvs_write_bool(key_name, config->io_trigger[i].input_enable);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save io trigger %d input enable to NVS", i);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_OUTPUT_EN_PREFIX, key_suffix);
        result = json_config_nvs_write_bool(key_name, config->io_trigger[i].output_enable);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save io trigger %d output enable to NVS", i);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_INPUT_TYPE_PREFIX, key_suffix);
        result = json_config_nvs_write_uint8(key_name, config->io_trigger[i].input_trigger_type);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save io trigger %d input type to NVS", i);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_OUTPUT_TYPE_PREFIX, key_suffix);
        result = json_config_nvs_write_uint8(key_name, config->io_trigger[i].output_trigger_type);
        if (result != AICAM_OK)
            LOG_CORE_ERROR("Failed to save io trigger %d output type to NVS", i);
    }
    LOG_CORE_INFO("Work mode configuration saved to NVS successfully");
    return result;
}

// save power mode configuration to NVS
aicam_result_t json_config_save_power_mode_config_to_nvs(const power_mode_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_uint32(NVS_KEY_POWER_CURRENT_MODE, (uint32_t)config->current_mode);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power current mode to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_POWER_DEFAULT_MODE, (uint32_t)config->default_mode);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power default mode to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_POWER_TIMEOUT, config->low_power_timeout_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power timeout to NVS");

    result = json_config_nvs_write_uint64(NVS_KEY_POWER_LAST_ACTIVITY, config->last_activity_time);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power last activity to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_POWER_SWITCH_COUNT, config->mode_switch_count);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power switch count to NVS");

    LOG_CORE_INFO("Power mode configuration saved to NVS successfully");
    return result;
}

// save device info configuration to NVS
aicam_result_t json_config_save_device_info_config_to_nvs(const device_info_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_NAME, config->device_name);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save device name to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_MAC, config->mac_address);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MAC address to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_SERIAL, config->serial_number);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save serial number to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_HW_VER, config->hardware_version);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save hardware version to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_FW_VER, config->software_version);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save firmware version to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_CAMERA, config->camera_module);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save camera module to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_EXTENSION, config->extension_modules);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save extension modules to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_STORAGE, config->storage_card_info);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save storage card info to NVS");

    result = json_config_nvs_write_float(NVS_KEY_DEVICE_INFO_STORAGE_PCT, config->storage_usage_percent);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save storage usage percent to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_POWER, config->power_supply_type);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power supply type to NVS");

    result = json_config_nvs_write_float(NVS_KEY_DEVICE_INFO_BATTERY_PCT, config->battery_percent);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save battery percent to NVS");

    result = json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_COMM, config->communication_type);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save communication type to NVS");

    LOG_CORE_INFO("Device info configuration saved to NVS successfully");
    return result;
}

// Save auth manager configuration to NVS
aicam_result_t json_config_save_auth_mgr_config_to_nvs(const auth_mgr_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    aicam_result_t result;

    result = json_config_nvs_write_uint32(NVS_KEY_AUTH_SESSION_TIMEOUT, config->session_timeout_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save auth session timeout to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_AUTH_ENABLE_TIMEOUT, config->enable_session_timeout);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save auth enable timeout to NVS");

    result = json_config_nvs_write_string(NVS_KEY_AUTH_PASSWORD, config->admin_password);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save admin password to NVS");

    LOG_CORE_INFO("Auth manager configuration saved to NVS successfully");
    return result;
}

// save device service image configuration to NVS
aicam_result_t json_config_save_device_service_image_config_to_nvs(const image_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_uint32(NVS_KEY_IMAGE_BRIGHTNESS, config->brightness);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image brightness to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_IMAGE_CONTRAST, config->contrast);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image contrast to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_IMAGE_HFLIP, config->horizontal_flip);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image horizontal flip to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_IMAGE_VFLIP, config->vertical_flip);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image vertical flip to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_IMAGE_AEC, config->aec);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image AEC to NVS");

    LOG_CORE_INFO("Device service image configuration saved to NVS successfully");
    return result;
}

// save device service light configuration to NVS
aicam_result_t json_config_save_device_service_light_config_to_nvs(const light_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_bool(NVS_KEY_LIGHT_CONNECTED, config->connected);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light connected to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_MODE, (uint32_t)config->mode);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light mode to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_START_HOUR, config->start_hour);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light start hour to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_START_MIN, config->start_minute);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light start minute to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_END_HOUR, config->end_hour);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light end hour to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_END_MIN, config->end_minute);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light end minute to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_BRIGHTNESS, config->brightness_level);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light brightness to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_LIGHT_AUTO_TRIGGER, config->auto_trigger_enabled);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light auto trigger to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_LIGHT_THRESHOLD, config->light_threshold);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light threshold to NVS");

    LOG_CORE_INFO("Device service light configuration saved to NVS successfully");
    return result;
}

// save network service configuration to NVS
aicam_result_t json_config_save_network_service_config_to_nvs(const network_service_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_uint32(NVS_KEY_NETWORK_AP_SLEEP_TIME, config->ap_sleep_time);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save AP sleep time to NVS");

    result = json_config_nvs_write_string(NVS_KEY_NETWORK_SSID, config->ssid);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save network SSID to NVS");

    result = json_config_nvs_write_string(NVS_KEY_NETWORK_PASSWORD, config->password);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save network password to NVS");

    // Save known_network_count
    result = json_config_nvs_write_uint32(NVS_KEY_NETWORK_KNOWN_COUNT, config->known_network_count);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save known network count to NVS");

    // Save known_networks array (limit to 16)
    uint32_t count = config->known_network_count > 16 ? 16 : config->known_network_count;
    for (unsigned int i = 0; i < count; i++)
    {
        char key_name[32];
        
        snprintf(key_name, sizeof(key_name), "net_%u_ssid", i);
        result = json_config_nvs_write_string(key_name, config->known_networks[i].ssid);
        
        snprintf(key_name, sizeof(key_name), "net_%u_bssid", i);
        result = json_config_nvs_write_string(key_name, config->known_networks[i].bssid);
        
        snprintf(key_name, sizeof(key_name), "net_%u_pwd", i);
        result = json_config_nvs_write_string(key_name, config->known_networks[i].password);
        
        snprintf(key_name, sizeof(key_name), "net_%u_rssi", i);
        result = json_config_nvs_write_int32(key_name, config->known_networks[i].rssi);
        
        snprintf(key_name, sizeof(key_name), "net_%u_ch", i);
        result = json_config_nvs_write_uint32(key_name, config->known_networks[i].channel);
        
        snprintf(key_name, sizeof(key_name), "net_%u_sec", i);
        result = json_config_nvs_write_uint32(key_name, (uint32_t)config->known_networks[i].security);
        
        snprintf(key_name, sizeof(key_name), "net_%u_conn", i);
        result = json_config_nvs_write_bool(key_name, config->known_networks[i].connected);
        
        snprintf(key_name, sizeof(key_name), "net_%u_known", i);
        result = json_config_nvs_write_bool(key_name, config->known_networks[i].is_known);
        
        snprintf(key_name, sizeof(key_name), "net_%u_time", i);
        result = json_config_nvs_write_uint32(key_name, config->known_networks[i].last_connected_time);
    }

    LOG_CORE_INFO("Network service configuration saved to NVS successfully");
    return result;
}

static aicam_result_t save_mqtt_base_config_to_nvs(const mqtt_base_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_PROTOCOL_VER, config->protocol_ver);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT protocol version to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_HOST, config->hostname);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT hostname to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_PORT, config->port);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT port to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_ID, config->client_id);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client ID to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_CLEAN_SESSION, config->clean_session);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT clean session to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_KEEPALIVE, config->keepalive);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT keepalive to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_USERNAME, config->username);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT username to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_PASSWORD, config->password);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT password to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CA_CERT_PATH, config->ca_cert_path);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT CA certificate path to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CA_CERT_DATA, config->ca_cert_data);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT CA certificate data to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_CA_CERT_LEN, config->ca_cert_len);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT CA certificate length to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_CERT_PATH, config->client_cert_path);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client certificate path to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_CERT_DATA, config->client_cert_data);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client certificate data to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_CLIENT_CERT_LEN, config->client_cert_len);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client certificate length to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_KEY_PATH, config->client_key_path);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client key path to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_KEY_DATA, config->client_key_data);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client key data to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_CLIENT_KEY_LEN, config->client_key_len);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT client key length to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_VERIFY_HOSTNAME, config->verify_hostname);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT verify hostname to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_LWT_TOPIC, config->lwt_topic);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT LWT topic to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_LWT_MESSAGE, config->lwt_message);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT LWT message to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_LWT_MSG_LEN, config->lwt_msg_len);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT LWT message length to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_LWT_QOS, config->lwt_qos);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT LWT QoS to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_LWT_RETAIN, config->lwt_retain);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT LWT retain to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_TASK_PRIORITY, config->task_priority);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT task priority to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_TASK_STACK, config->task_stack_size);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT task stack size to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_DISABLE_RECONNECT, config->disable_auto_reconnect);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT disable auto reconnect to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_OUTBOX_LIMIT, config->outbox_limit);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT outbox limit to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_OUTBOX_RESEND_IV, config->outbox_resend_interval_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT outbox resend interval to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_OUTBOX_EXPIRE, config->outbox_expired_timeout_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT outbox expired timeout to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_RECONNECT_INTERVAL, config->reconnect_interval_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT reconnect interval to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_TIMEOUT, config->timeout_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT timeout to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_BUFFER_SIZE, config->buffer_size);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT buffer size to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_TX_BUF_SIZE, config->tx_buf_size);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT TX buffer size to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_RX_BUF_SIZE, config->rx_buf_size);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT RX buffer size to NVS");

    LOG_CORE_INFO("MQTT base service configuration saved to NVS successfully");
    return result;
}

// save mqtt service configuration to NVS
aicam_result_t json_config_save_mqtt_service_config_to_nvs(const mqtt_service_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }
    aicam_result_t result = AICAM_OK;

    // base configuration
    result = save_mqtt_base_config_to_nvs(&config->base_config);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT base configuration to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_RECV_TOPIC, config->data_receive_topic);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT data receive topic to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_REPORT_TOPIC, config->data_report_topic);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT data report topic to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_STATUS_TOPIC, config->status_topic);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT status topic to NVS");

    result = json_config_nvs_write_string(NVS_KEY_MQTT_CMD_TOPIC, config->command_topic);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT command topic to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_RECV_QOS, config->data_receive_qos);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT data receive QoS to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_REPORT_QOS, config->data_report_qos);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT data report QoS to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_STATUS_QOS, config->status_qos);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT status QoS to NVS");

    result = json_config_nvs_write_uint8(NVS_KEY_MQTT_CMD_QOS, config->command_qos);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT command QoS to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_MQTT_AUTO_SUB_RECV, config->auto_subscribe_receive);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT auto subscribe receive to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_MQTT_AUTO_SUB_CMD, config->auto_subscribe_command);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT auto subscribe command to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_MQTT_ENABLE_STATUS, config->enable_status_report);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT enable status report to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_STATUS_INTERVAL, config->status_report_interval_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT status report interval to NVS");

    result = json_config_nvs_write_bool(NVS_KEY_MQTT_ENABLE_HEARTBEAT, config->enable_heartbeat);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT enable heartbeat to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MQTT_HEARTBEAT_INTERVAL, config->heartbeat_interval_ms);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT heartbeat interval to NVS");

    LOG_CORE_INFO("MQTT full service configuration saved to NVS successfully");
    return result;
}

aicam_result_t json_config_save_to_nvs(const aicam_global_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR_INVALID_PARAM;
    }

    aicam_result_t result;

    // Save basic configuration information
    result = json_config_nvs_write_uint32(NVS_KEY_CONFIG_VERSION, config->config_version);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save config version to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_MAGIC_NUMBER, config->magic_number);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save magic number to NVS");

    result = json_config_nvs_write_uint32(NVS_KEY_CHECKSUM, config->checksum);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save checksum to NVS");

    result = json_config_nvs_write_uint64(NVS_KEY_TIMESTAMP, config->timestamp);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save timestamp to NVS");

    // Save log configuration
    result = json_config_save_log_config_to_nvs(&config->log_config);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save log configuration to NVS");

    // Save ai debug configuration
    result = json_config_save_ai_debug_config_to_nvs(&config->ai_debug);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save ai debug configuration to NVS");

    // Save work mode configuration
    result = json_config_save_work_mode_config_to_nvs(&config->work_mode_config);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save work mode configuration to NVS");

    // Save power mode configuration
    result = json_config_save_power_mode_config_to_nvs(&config->power_mode_config);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save power mode configuration to NVS");

    // Save device info configuration
    result = json_config_save_device_info_config_to_nvs(&config->device_info);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save device info configuration to NVS");

    // Save device service configuration - image config
    result = json_config_save_device_service_image_config_to_nvs(&config->device_service.image_config);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save image configuration to NVS");

    // Save device service configuration - light config
    result = json_config_save_device_service_light_config_to_nvs(&config->device_service.light_config);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save light configuration to NVS");

    // Save network service configuration
    result = json_config_save_network_service_config_to_nvs(&config->network_service);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save network configuration to NVS");

    // Save MQTT service configuration - base config (persistable, no pointers)
    result = json_config_save_mqtt_service_config_to_nvs(&config->mqtt_service);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save MQTT service configuration to NVS");

    // Save auth manager configuration
    result = json_config_save_auth_mgr_config_to_nvs(&config->auth_mgr);
    if (result != AICAM_OK)
        LOG_CORE_ERROR("Failed to save auth manager configuration to NVS");

    LOG_CORE_INFO("All config saved to NVS successfully");
    return AICAM_OK;
}

aicam_result_t json_config_load_from_nvs(aicam_global_config_t *config)
{
    if (!config)
    {
        return AICAM_ERROR;
    }

    // First load default configuration as a base
    memcpy(config, &default_config, sizeof(aicam_global_config_t));

    aicam_result_t result;
    int32_t temp_int32;
    uint32_t temp_uint32;
    uint64_t temp_uint64;
    uint8_t temp_uint8;
    aicam_bool_t temp_bool;

    // Load basic configuration information
    result = json_config_nvs_read_uint32(NVS_KEY_CONFIG_VERSION, &temp_uint32);
    if (result == AICAM_OK)
        config->config_version = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_CONFIG_VERSION, config->config_version);

    result = json_config_nvs_read_uint32(NVS_KEY_MAGIC_NUMBER, &temp_uint32);
    if (result == AICAM_OK)
        config->magic_number = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MAGIC_NUMBER, config->magic_number);

    result = json_config_nvs_read_uint32(NVS_KEY_CHECKSUM, &temp_uint32);
    if (result == AICAM_OK)
        config->checksum = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_CHECKSUM, config->checksum);

    result = json_config_nvs_read_uint64(NVS_KEY_TIMESTAMP, &temp_uint64);
    if (result == AICAM_OK)
        config->timestamp = temp_uint64;
    else
        json_config_nvs_write_uint64(NVS_KEY_TIMESTAMP, config->timestamp);

    // Load log configuration
    result = json_config_nvs_read_uint8(NVS_KEY_LOG_LEVEL, &temp_uint8);
    if (result == AICAM_OK)
        config->log_config.log_level = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_LOG_LEVEL, config->log_config.log_level);

    result = json_config_nvs_read_uint32(NVS_KEY_LOG_FILE_SIZE, &temp_uint32);
    if (result == AICAM_OK)
        config->log_config.log_file_size_kb = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LOG_FILE_SIZE, config->log_config.log_file_size_kb);

    result = json_config_nvs_read_uint32(NVS_KEY_LOG_FILE_COUNT, &temp_uint32);
    if (result == AICAM_OK)
        config->log_config.log_file_count = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LOG_FILE_COUNT, config->log_config.log_file_count);

    // Load ai debug configuration
    result = json_config_nvs_read_bool(NVS_KEY_AI_ENABLE, &temp_bool);
    if (result == AICAM_OK)
        config->ai_debug.ai_enabled = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_AI_ENABLE, config->ai_debug.ai_enabled);

    result = json_config_nvs_read_bool(NVS_KEY_AI_1_ACTIVE, &temp_bool);
    if (result == AICAM_OK)
        config->ai_debug.ai_1_active = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_AI_1_ACTIVE, config->ai_debug.ai_1_active);

    result = json_config_nvs_read_uint32(NVS_KEY_CONFIDENCE, &temp_uint32);
    if (result == AICAM_OK)
        config->ai_debug.confidence_threshold = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_CONFIDENCE, config->ai_debug.confidence_threshold);

    result = json_config_nvs_read_uint32(NVS_KEY_NMS_THRESHOLD, &temp_uint32);
    if (result == AICAM_OK)
        config->ai_debug.nms_threshold = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_NMS_THRESHOLD, config->ai_debug.nms_threshold);

    // Load power mode configuration
    result = json_config_nvs_read_uint32(NVS_KEY_POWER_CURRENT_MODE, &temp_uint32);
    if (result == AICAM_OK)
        config->power_mode_config.current_mode = (power_mode_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_POWER_CURRENT_MODE, (uint32_t)config->power_mode_config.current_mode);

    result = json_config_nvs_read_uint32(NVS_KEY_POWER_DEFAULT_MODE, &temp_uint32);
    if (result == AICAM_OK)
        config->power_mode_config.default_mode = (power_mode_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_POWER_DEFAULT_MODE, (uint32_t)config->power_mode_config.default_mode);

    result = json_config_nvs_read_uint32(NVS_KEY_POWER_TIMEOUT, &temp_uint32);
    if (result == AICAM_OK)
        config->power_mode_config.low_power_timeout_ms = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_POWER_TIMEOUT, config->power_mode_config.low_power_timeout_ms);

    result = json_config_nvs_read_uint64(NVS_KEY_POWER_LAST_ACTIVITY, &temp_uint64);
    if (result == AICAM_OK)
        config->power_mode_config.last_activity_time = temp_uint64;
    else
        json_config_nvs_write_uint64(NVS_KEY_POWER_LAST_ACTIVITY, config->power_mode_config.last_activity_time);

    result = json_config_nvs_read_uint32(NVS_KEY_POWER_SWITCH_COUNT, &temp_uint32);
    if (result == AICAM_OK)
        config->power_mode_config.mode_switch_count = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_POWER_SWITCH_COUNT, config->power_mode_config.mode_switch_count);

    // Load device info configuration
    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_NAME, config->device_info.device_name, sizeof(config->device_info.device_name));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_NAME, config->device_info.device_name);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_MAC, config->device_info.mac_address, sizeof(config->device_info.mac_address));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_MAC, config->device_info.mac_address);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_SERIAL, config->device_info.serial_number, sizeof(config->device_info.serial_number));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_SERIAL, config->device_info.serial_number);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_HW_VER, config->device_info.hardware_version, sizeof(config->device_info.hardware_version));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_HW_VER, config->device_info.hardware_version);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_FW_VER, config->device_info.software_version, sizeof(config->device_info.software_version));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_FW_VER, config->device_info.software_version);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_CAMERA, config->device_info.camera_module, sizeof(config->device_info.camera_module));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_CAMERA, config->device_info.camera_module);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_EXTENSION, config->device_info.extension_modules, sizeof(config->device_info.extension_modules));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_EXTENSION, config->device_info.extension_modules);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_STORAGE, config->device_info.storage_card_info, sizeof(config->device_info.storage_card_info));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_STORAGE, config->device_info.storage_card_info);

    result = json_config_nvs_read_float(NVS_KEY_DEVICE_INFO_STORAGE_PCT, &config->device_info.storage_usage_percent);
    if (result != AICAM_OK)
        json_config_nvs_write_float(NVS_KEY_DEVICE_INFO_STORAGE_PCT, config->device_info.storage_usage_percent);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_POWER, config->device_info.power_supply_type, sizeof(config->device_info.power_supply_type));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_POWER, config->device_info.power_supply_type);

    result = json_config_nvs_read_float(NVS_KEY_DEVICE_INFO_BATTERY_PCT, &config->device_info.battery_percent);
    if (result != AICAM_OK)
        json_config_nvs_write_float(NVS_KEY_DEVICE_INFO_BATTERY_PCT, config->device_info.battery_percent);

    result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_COMM, config->device_info.communication_type, sizeof(config->device_info.communication_type));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_DEVICE_INFO_COMM, config->device_info.communication_type);

    // Load auth manager configuration
    result = json_config_nvs_read_uint32(NVS_KEY_AUTH_SESSION_TIMEOUT, &temp_uint32);
    if (result == AICAM_OK)
        config->auth_mgr.session_timeout_ms = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_AUTH_SESSION_TIMEOUT, config->auth_mgr.session_timeout_ms);

    result = json_config_nvs_read_bool(NVS_KEY_AUTH_ENABLE_TIMEOUT, &temp_bool);
    if (result == AICAM_OK)
        config->auth_mgr.enable_session_timeout = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_AUTH_ENABLE_TIMEOUT, config->auth_mgr.enable_session_timeout);

    // Try new key first, fallback to old key for backward compatibility
    result = json_config_nvs_read_string(NVS_KEY_AUTH_PASSWORD, config->auth_mgr.admin_password, sizeof(config->auth_mgr.admin_password));
    if (result != AICAM_OK) {
        // Fallback to old key for backward compatibility
        result = json_config_nvs_read_string(NVS_KEY_DEVICE_INFO_PASSWORD, config->auth_mgr.admin_password, sizeof(config->auth_mgr.admin_password));
        if (result == AICAM_OK) {
            // Migrate to new key
            json_config_nvs_write_string(NVS_KEY_AUTH_PASSWORD, config->auth_mgr.admin_password);
        } else {
            json_config_nvs_write_string(NVS_KEY_AUTH_PASSWORD, config->auth_mgr.admin_password);
        }
    }

    // Load device service configuration - image config
    result = json_config_nvs_read_uint32(NVS_KEY_IMAGE_BRIGHTNESS, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.image_config.brightness = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_IMAGE_BRIGHTNESS, config->device_service.image_config.brightness);

    result = json_config_nvs_read_uint32(NVS_KEY_IMAGE_CONTRAST, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.image_config.contrast = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_IMAGE_CONTRAST, config->device_service.image_config.contrast);

    result = json_config_nvs_read_bool(NVS_KEY_IMAGE_HFLIP, &temp_bool);
    if (result == AICAM_OK)
        config->device_service.image_config.horizontal_flip = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_IMAGE_HFLIP, config->device_service.image_config.horizontal_flip);

    result = json_config_nvs_read_bool(NVS_KEY_IMAGE_VFLIP, &temp_bool);
    if (result == AICAM_OK)
        config->device_service.image_config.vertical_flip = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_IMAGE_VFLIP, config->device_service.image_config.vertical_flip);

    result = json_config_nvs_read_uint32(NVS_KEY_IMAGE_AEC, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.image_config.aec = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_IMAGE_AEC, config->device_service.image_config.aec);

    // Load device service configuration - light config
    result = json_config_nvs_read_bool(NVS_KEY_LIGHT_CONNECTED, &temp_bool);
    if (result == AICAM_OK)
        config->device_service.light_config.connected = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_LIGHT_CONNECTED, config->device_service.light_config.connected);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_MODE, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.mode = (light_mode_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_MODE, (uint32_t)config->device_service.light_config.mode);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_START_HOUR, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.start_hour = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_START_HOUR, config->device_service.light_config.start_hour);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_START_MIN, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.start_minute = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_START_MIN, config->device_service.light_config.start_minute);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_END_HOUR, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.end_hour = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_END_HOUR, config->device_service.light_config.end_hour);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_END_MIN, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.end_minute = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_END_MIN, config->device_service.light_config.end_minute);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_BRIGHTNESS, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.brightness_level = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_BRIGHTNESS, config->device_service.light_config.brightness_level);

    result = json_config_nvs_read_bool(NVS_KEY_LIGHT_AUTO_TRIGGER, &temp_bool);
    if (result == AICAM_OK)
        config->device_service.light_config.auto_trigger_enabled = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_LIGHT_AUTO_TRIGGER, config->device_service.light_config.auto_trigger_enabled);

    result = json_config_nvs_read_uint32(NVS_KEY_LIGHT_THRESHOLD, &temp_uint32);
    if (result == AICAM_OK)
        config->device_service.light_config.light_threshold = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_LIGHT_THRESHOLD, config->device_service.light_config.light_threshold);

    // Load network service configuration
    result = json_config_nvs_read_uint32(NVS_KEY_NETWORK_AP_SLEEP_TIME, &temp_uint32);
    if (result == AICAM_OK)
        config->network_service.ap_sleep_time = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_NETWORK_AP_SLEEP_TIME, config->network_service.ap_sleep_time);

    result = json_config_nvs_read_string(NVS_KEY_NETWORK_SSID, config->network_service.ssid, sizeof(config->network_service.ssid));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_NETWORK_SSID, config->network_service.ssid);

    result = json_config_nvs_read_string(NVS_KEY_NETWORK_PASSWORD, config->network_service.password, sizeof(config->network_service.password));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_NETWORK_PASSWORD, config->network_service.password);

    // Load known_network_count
    result = json_config_nvs_read_uint32(NVS_KEY_NETWORK_KNOWN_COUNT, &temp_uint32);
    if (result == AICAM_OK) {
        config->network_service.known_network_count = temp_uint32 > 16 ? 16 : temp_uint32;
    } else {
        json_config_nvs_write_uint32(NVS_KEY_NETWORK_KNOWN_COUNT, config->network_service.known_network_count);
    }

    // Load known_networks array
    for (unsigned int i = 0; i < config->network_service.known_network_count; i++)
    {
        char key_name[32];
        
        snprintf(key_name, sizeof(key_name), "net_%u_ssid", i);
        result = json_config_nvs_read_string(key_name, config->network_service.known_networks[i].ssid, 
                                            sizeof(config->network_service.known_networks[i].ssid));
        
        snprintf(key_name, sizeof(key_name), "net_%u_bssid", i);
        result = json_config_nvs_read_string(key_name, config->network_service.known_networks[i].bssid, 
                                            sizeof(config->network_service.known_networks[i].bssid));
        
        snprintf(key_name, sizeof(key_name), "net_%u_pwd", i);
        result = json_config_nvs_read_string(key_name, config->network_service.known_networks[i].password, 
                                            sizeof(config->network_service.known_networks[i].password));
        
        snprintf(key_name, sizeof(key_name), "net_%u_rssi", i);
        if (json_config_nvs_read_int32(key_name, &temp_int32) == AICAM_OK) {
            config->network_service.known_networks[i].rssi = temp_int32;
        }
        
        snprintf(key_name, sizeof(key_name), "net_%u_ch", i);
        result = json_config_nvs_read_uint32(key_name, &config->network_service.known_networks[i].channel);
        
        snprintf(key_name, sizeof(key_name), "net_%u_sec", i);
        if (json_config_nvs_read_uint32(key_name, &temp_uint32) == AICAM_OK) {
            config->network_service.known_networks[i].security = (wireless_security_t)temp_uint32;
        }
        
        snprintf(key_name, sizeof(key_name), "net_%u_conn", i);
        result = json_config_nvs_read_bool(key_name, &config->network_service.known_networks[i].connected);
        
        snprintf(key_name, sizeof(key_name), "net_%u_known", i);
        result = json_config_nvs_read_bool(key_name, &config->network_service.known_networks[i].is_known);
        
        snprintf(key_name, sizeof(key_name), "net_%u_time", i);
        result = json_config_nvs_read_uint32(key_name, &config->network_service.known_networks[i].last_connected_time);
    }

    // Load MQTT service configuration - base config (persistable, no pointers)
    // Basic connection
    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_PROTOCOL_VER, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.protocol_ver = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_PROTOCOL_VER, config->mqtt_service.base_config.protocol_ver);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_HOST, config->mqtt_service.base_config.hostname, sizeof(config->mqtt_service.base_config.hostname));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_HOST, config->mqtt_service.base_config.hostname);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_PORT, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.port = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_PORT, (uint32_t)config->mqtt_service.base_config.port);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_CLIENT_ID, config->mqtt_service.base_config.client_id, sizeof(config->mqtt_service.base_config.client_id));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_ID, config->mqtt_service.base_config.client_id);

    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_CLEAN_SESSION, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.clean_session = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_CLEAN_SESSION, config->mqtt_service.base_config.clean_session);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_KEEPALIVE, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.keepalive = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_KEEPALIVE, (uint32_t)config->mqtt_service.base_config.keepalive);

    // Authentication
    result = json_config_nvs_read_string(NVS_KEY_MQTT_USERNAME, config->mqtt_service.base_config.username, sizeof(config->mqtt_service.base_config.username));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_USERNAME, config->mqtt_service.base_config.username);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_PASSWORD, config->mqtt_service.base_config.password, sizeof(config->mqtt_service.base_config.password));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_PASSWORD, config->mqtt_service.base_config.password);

    // SSL/TLS - CA certificate
    result = json_config_nvs_read_string(NVS_KEY_MQTT_CA_CERT_PATH, config->mqtt_service.base_config.ca_cert_path, sizeof(config->mqtt_service.base_config.ca_cert_path));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CA_CERT_PATH, config->mqtt_service.base_config.ca_cert_path);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_CA_CERT_DATA, config->mqtt_service.base_config.ca_cert_data, sizeof(config->mqtt_service.base_config.ca_cert_data));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CA_CERT_DATA, config->mqtt_service.base_config.ca_cert_data);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_CA_CERT_LEN, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.ca_cert_len = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_CA_CERT_LEN, (uint32_t)config->mqtt_service.base_config.ca_cert_len);

    // SSL/TLS - Client certificate
    result = json_config_nvs_read_string(NVS_KEY_MQTT_CLIENT_CERT_PATH, config->mqtt_service.base_config.client_cert_path, sizeof(config->mqtt_service.base_config.client_cert_path));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_CERT_PATH, config->mqtt_service.base_config.client_cert_path);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_CLIENT_CERT_DATA, config->mqtt_service.base_config.client_cert_data, sizeof(config->mqtt_service.base_config.client_cert_data));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_CERT_DATA, config->mqtt_service.base_config.client_cert_data);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_CLIENT_CERT_LEN, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.client_cert_len = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_CLIENT_CERT_LEN, (uint32_t)config->mqtt_service.base_config.client_cert_len);

    // SSL/TLS - Client key
    result = json_config_nvs_read_string(NVS_KEY_MQTT_CLIENT_KEY_PATH, config->mqtt_service.base_config.client_key_path, sizeof(config->mqtt_service.base_config.client_key_path));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_KEY_PATH, config->mqtt_service.base_config.client_key_path);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_CLIENT_KEY_DATA, config->mqtt_service.base_config.client_key_data, sizeof(config->mqtt_service.base_config.client_key_data));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CLIENT_KEY_DATA, config->mqtt_service.base_config.client_key_data);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_CLIENT_KEY_LEN, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.client_key_len = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_CLIENT_KEY_LEN, (uint32_t)config->mqtt_service.base_config.client_key_len);

    // SSL/TLS - Settings
    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_VERIFY_HOSTNAME, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.verify_hostname = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_VERIFY_HOSTNAME, config->mqtt_service.base_config.verify_hostname);

    // Last Will and Testament
    result = json_config_nvs_read_string(NVS_KEY_MQTT_LWT_TOPIC, config->mqtt_service.base_config.lwt_topic, sizeof(config->mqtt_service.base_config.lwt_topic));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_LWT_TOPIC, config->mqtt_service.base_config.lwt_topic);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_LWT_MESSAGE, config->mqtt_service.base_config.lwt_message, sizeof(config->mqtt_service.base_config.lwt_message));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_LWT_MESSAGE, config->mqtt_service.base_config.lwt_message);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_LWT_MSG_LEN, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.lwt_msg_len = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_LWT_MSG_LEN, (uint32_t)config->mqtt_service.base_config.lwt_msg_len);

    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_LWT_QOS, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.lwt_qos = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_LWT_QOS, config->mqtt_service.base_config.lwt_qos);

    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_LWT_RETAIN, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.lwt_retain = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_LWT_RETAIN, config->mqtt_service.base_config.lwt_retain);

    // Task parameters
    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_TASK_PRIORITY, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.task_priority = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_TASK_PRIORITY, (uint32_t)config->mqtt_service.base_config.task_priority);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_TASK_STACK, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.task_stack_size = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_TASK_STACK, config->mqtt_service.base_config.task_stack_size);

    // Network parameters
    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_DISABLE_RECONNECT, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.disable_auto_reconnect = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_DISABLE_RECONNECT, config->mqtt_service.base_config.disable_auto_reconnect);

    result = json_config_nvs_read_uint8(NVS_KEY_MQTT_OUTBOX_LIMIT, &temp_uint8);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.outbox_limit = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_MQTT_OUTBOX_LIMIT, config->mqtt_service.base_config.outbox_limit);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_OUTBOX_RESEND_IV, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.outbox_resend_interval_ms = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_OUTBOX_RESEND_IV, (uint32_t)config->mqtt_service.base_config.outbox_resend_interval_ms);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_OUTBOX_EXPIRE, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.outbox_expired_timeout_ms = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_OUTBOX_EXPIRE, (uint32_t)config->mqtt_service.base_config.outbox_expired_timeout_ms);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_RECONNECT_INTERVAL, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.reconnect_interval_ms = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_RECONNECT_INTERVAL, (uint32_t)config->mqtt_service.base_config.reconnect_interval_ms);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_TIMEOUT, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.timeout_ms = (uint16_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_TIMEOUT, (uint32_t)config->mqtt_service.base_config.timeout_ms);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_BUFFER_SIZE, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.buffer_size = temp_uint32;   
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_BUFFER_SIZE, config->mqtt_service.base_config.buffer_size);
    

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_TX_BUF_SIZE, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.tx_buf_size = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_TX_BUF_SIZE, config->mqtt_service.base_config.tx_buf_size);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_RX_BUF_SIZE, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.base_config.rx_buf_size = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_RX_BUF_SIZE, config->mqtt_service.base_config.rx_buf_size);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_RECV_TOPIC, config->mqtt_service.data_receive_topic, sizeof(config->mqtt_service.data_receive_topic));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_RECV_TOPIC, config->mqtt_service.data_receive_topic);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_REPORT_TOPIC, config->mqtt_service.data_report_topic, sizeof(config->mqtt_service.data_report_topic));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_REPORT_TOPIC, config->mqtt_service.data_report_topic);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_STATUS_TOPIC, config->mqtt_service.status_topic, sizeof(config->mqtt_service.status_topic));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_STATUS_TOPIC, config->mqtt_service.status_topic);

    result = json_config_nvs_read_string(NVS_KEY_MQTT_CMD_TOPIC, config->mqtt_service.command_topic, sizeof(config->mqtt_service.command_topic));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_MQTT_CMD_TOPIC, config->mqtt_service.command_topic);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_RECV_QOS, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.data_receive_qos = (int)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_RECV_QOS, (uint32_t)config->mqtt_service.data_receive_qos);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_REPORT_QOS, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.data_report_qos = (int)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_REPORT_QOS, (uint32_t)config->mqtt_service.data_report_qos);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_STATUS_QOS, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.status_qos = (int)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_STATUS_QOS, (uint32_t)config->mqtt_service.status_qos);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_CMD_QOS, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.command_qos = (int)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_CMD_QOS, (uint32_t)config->mqtt_service.command_qos);

    result = json_config_nvs_read_bool(NVS_KEY_MQTT_AUTO_SUB_RECV, &temp_bool);
    if (result == AICAM_OK)
        config->mqtt_service.auto_subscribe_receive = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_MQTT_AUTO_SUB_RECV, config->mqtt_service.auto_subscribe_receive);

    result = json_config_nvs_read_bool(NVS_KEY_MQTT_AUTO_SUB_CMD, &temp_bool);
    if (result == AICAM_OK)
        config->mqtt_service.auto_subscribe_command = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_MQTT_AUTO_SUB_CMD, config->mqtt_service.auto_subscribe_command);

    result = json_config_nvs_read_bool(NVS_KEY_MQTT_ENABLE_STATUS, &temp_bool);
    if (result == AICAM_OK)
        config->mqtt_service.enable_status_report = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_MQTT_ENABLE_STATUS, config->mqtt_service.enable_status_report);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_STATUS_INTERVAL, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.status_report_interval_ms = (int)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_STATUS_INTERVAL, (uint32_t)config->mqtt_service.status_report_interval_ms);

    result = json_config_nvs_read_bool(NVS_KEY_MQTT_ENABLE_HEARTBEAT, &temp_bool);
    if (result == AICAM_OK)
        config->mqtt_service.enable_heartbeat = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_MQTT_ENABLE_HEARTBEAT, config->mqtt_service.enable_heartbeat);

    result = json_config_nvs_read_uint32(NVS_KEY_MQTT_HEARTBEAT_INTERVAL, &temp_uint32);
    if (result == AICAM_OK)
        config->mqtt_service.heartbeat_interval_ms = (int)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_MQTT_HEARTBEAT_INTERVAL, (uint32_t)config->mqtt_service.heartbeat_interval_ms);

    // Load work mode configuration
    result = json_config_nvs_read_uint32(NVS_KEY_WORK_MODE, &temp_uint32);
    if (result == AICAM_OK)
        config->work_mode_config.work_mode = (aicam_work_mode_t)temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_WORK_MODE, (uint32_t)config->work_mode_config.work_mode);

    // Load image mode enable
    result = json_config_nvs_read_bool(NVS_KEY_IMAGE_MODE_ENABLE, &temp_bool);
    if (result == AICAM_OK)
        config->work_mode_config.image_mode.enable = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_IMAGE_MODE_ENABLE, config->work_mode_config.image_mode.enable);

    // Load video stream mode enable
    result = json_config_nvs_read_bool(NVS_KEY_VIDEO_STREAM_MODE_ENABLE, &temp_bool);
    if (result == AICAM_OK)
        config->work_mode_config.video_stream_mode.enable = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_VIDEO_STREAM_MODE_ENABLE, config->work_mode_config.video_stream_mode.enable);

    result = json_config_nvs_read_bool(NVS_KEY_PIR_ENABLE, &temp_bool);
    if (result == AICAM_OK)
        config->work_mode_config.pir_trigger.enable = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_PIR_ENABLE, config->work_mode_config.pir_trigger.enable);

    result = json_config_nvs_read_uint8(NVS_KEY_PIR_PIN, &temp_uint8);
    if (result == AICAM_OK)
        config->work_mode_config.pir_trigger.pin_number = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_PIR_PIN, config->work_mode_config.pir_trigger.pin_number);

    result = json_config_nvs_read_uint8(NVS_KEY_PIR_TRIGGER_TYPE, &temp_uint8);
    if (result == AICAM_OK)
        config->work_mode_config.pir_trigger.trigger_type = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_PIR_TRIGGER_TYPE, config->work_mode_config.pir_trigger.trigger_type);

    // Load IO trigger configuration (array of IO_TRIGGER_MAX triggers)
    for (int i = 0; i < IO_TRIGGER_MAX; i++)
    {
        char key_suffix[16];
        snprintf(key_suffix, sizeof(key_suffix), "_%d", i); // e.g., "_0"

        char key_name[32];

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_ENABLE_PREFIX, key_suffix);
        result = json_config_nvs_read_bool(key_name, &config->work_mode_config.io_trigger[i].enable);
        if (result != AICAM_OK)
            json_config_nvs_write_bool(key_name, config->work_mode_config.io_trigger[i].enable);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_PIN_PREFIX, key_suffix);
        result = json_config_nvs_read_uint32(key_name, &config->work_mode_config.io_trigger[i].pin_number);
        if (result != AICAM_OK)
            json_config_nvs_write_uint32(key_name, config->work_mode_config.io_trigger[i].pin_number);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_INPUT_EN_PREFIX, key_suffix);
        result = json_config_nvs_read_bool(key_name, &config->work_mode_config.io_trigger[i].input_enable);
        if (result != AICAM_OK)
            json_config_nvs_write_bool(key_name, config->work_mode_config.io_trigger[i].input_enable);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_OUTPUT_EN_PREFIX, key_suffix);
        result = json_config_nvs_read_bool(key_name, &config->work_mode_config.io_trigger[i].output_enable);
        if (result != AICAM_OK)
            json_config_nvs_write_bool(key_name, config->work_mode_config.io_trigger[i].output_enable);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_INPUT_TYPE_PREFIX, key_suffix);
        result = json_config_nvs_read_uint8(key_name, &config->work_mode_config.io_trigger[i].input_trigger_type);
        if (result != AICAM_OK)
            json_config_nvs_write_uint8(key_name, config->work_mode_config.io_trigger[i].input_trigger_type);

        snprintf(key_name, sizeof(key_name), "%s%s", NVS_KEY_IO_OUTPUT_TYPE_PREFIX, key_suffix);
        result = json_config_nvs_read_uint8(key_name, &config->work_mode_config.io_trigger[i].output_trigger_type);
        if (result != AICAM_OK)
            json_config_nvs_write_uint8(key_name, config->work_mode_config.io_trigger[i].output_trigger_type);
    }

    result = json_config_nvs_read_bool(NVS_KEY_TIMER_ENABLE, &temp_bool);
    if (result == AICAM_OK)
        config->work_mode_config.timer_trigger.enable = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_TIMER_ENABLE, config->work_mode_config.timer_trigger.enable);

    result = json_config_nvs_read_uint8(NVS_KEY_TIMER_CAPTURE_MODE, &temp_uint8);
    if (result == AICAM_OK)
        config->work_mode_config.timer_trigger.capture_mode = temp_uint8;
    else
        json_config_nvs_write_uint8(NVS_KEY_TIMER_CAPTURE_MODE, config->work_mode_config.timer_trigger.capture_mode);

    result = json_config_nvs_read_uint32(NVS_KEY_TIMER_INTERVAL, &temp_uint32);
    if (result == AICAM_OK)
        config->work_mode_config.timer_trigger.interval_sec = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_TIMER_INTERVAL, config->work_mode_config.timer_trigger.interval_sec);

    result = json_config_nvs_read_uint32(NVS_KEY_TIMER_NODE_COUNT, &temp_uint32);
    if (result == AICAM_OK)
        config->work_mode_config.timer_trigger.time_node_count = temp_uint32;
    else
        json_config_nvs_write_uint32(NVS_KEY_TIMER_NODE_COUNT, config->work_mode_config.timer_trigger.time_node_count);

    // Load time nodes array
    for (int i = 0; i < config->work_mode_config.timer_trigger.time_node_count; i++)
    {
        char key_name[32];
        snprintf(key_name, sizeof(key_name), "%s%d", NVS_KEY_TIMER_NODE_PREFIX, i);
        result = json_config_nvs_read_uint32(key_name, &config->work_mode_config.timer_trigger.time_node[i]);
        if (result != AICAM_OK)
            json_config_nvs_write_uint32(key_name, config->work_mode_config.timer_trigger.time_node[i]);
    }

    // Load weekdays array
    for (int i = 0; i < config->work_mode_config.timer_trigger.time_node_count; i++)
    {
        char key_name[32];
        snprintf(key_name, sizeof(key_name), "%s%d", NVS_KEY_TIMER_WEEKDAYS_PREFIX, i);
        result = json_config_nvs_read_uint8(key_name, &config->work_mode_config.timer_trigger.weekdays[i]);
        if (result != AICAM_OK)
            json_config_nvs_write_uint8(key_name, config->work_mode_config.timer_trigger.weekdays[i]);
    }

    result = json_config_nvs_read_string(NVS_KEY_RTSP_URL, config->work_mode_config.video_stream_mode.rtsp_server_url, sizeof(config->work_mode_config.video_stream_mode.rtsp_server_url));
    if (result != AICAM_OK)
        json_config_nvs_write_string(NVS_KEY_RTSP_URL, config->work_mode_config.video_stream_mode.rtsp_server_url);

    result = json_config_nvs_read_bool(NVS_KEY_REMOTE_TRIGGER_ENABLE, &temp_bool);
    if (result == AICAM_OK)
        config->work_mode_config.remote_trigger.enable = temp_bool;
    else
        json_config_nvs_write_bool(NVS_KEY_REMOTE_TRIGGER_ENABLE, config->work_mode_config.remote_trigger.enable);

    LOG_CORE_INFO("Config loaded from NVS successfully");
    return AICAM_OK;
}

/* ==================== NVS Helper Functions Implementation ==================== */

// Note: These functions are no longer 'static' as they are part of the
// internal API defined in json_config_internal.h

aicam_result_t json_config_nvs_write_string(const char *key, const char *value)
{
    int result = storage_nvs_write(NVS_USER, key, value, strlen(value) + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_string(const char *key, char *value, size_t max_len)
{
    int result = storage_nvs_read(NVS_USER, key, value, max_len);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}


aicam_result_t json_config_nvs_write_uint32(const char *key, uint32_t value)
{
    char value_str[12];
    snprintf(value_str, sizeof(value_str), "%lu", value);
    int result = storage_nvs_write(NVS_USER, key, value_str, strlen(value_str) + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_uint32(const char *key, uint32_t *value)
{
    char value_str[12];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0)
    {
        *value = (uint32_t)strtoul(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

aicam_result_t json_config_nvs_write_uint64(const char *key, uint64_t value)
{
    char value_str[21] = {0};
    int i = 0;

    if (value == 0) {
        value_str[i++] = '0';
    } else {
        while (value > 0 && i < 20) {
            value_str[i++] = '0' + (value % 10);
            value /= 10;
        }
    }
    value_str[i] = '\0';
    for (int j = 0; j < i / 2; j++) {
        char tmp = value_str[j];
        value_str[j] = value_str[i - 1 - j];
        value_str[i - 1 - j] = tmp;
    }
    int result = storage_nvs_write(NVS_USER, key, value_str, i + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_uint64(const char *key, uint64_t *value)
{
    char value_str[21] = {0};
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result < 0) {
        return AICAM_ERROR;
    }

    uint64_t val = 0;
    const char *p = value_str;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    while (*p >= '0' && *p <= '9') {
        uint8_t digit = *p - '0';
        if (val > (UINT64_MAX - digit) / 10) {
            return AICAM_ERROR;
        }
        val = val * 10 + digit;
        p++;
    }

    *value = val;
    return AICAM_OK;
}

aicam_result_t json_config_nvs_write_float(const char *key, float value)
{
    char value_str[16];
    snprintf(value_str, sizeof(value_str), "%.6f", value);
    int result = storage_nvs_write(NVS_USER, key, value_str, strlen(value_str) + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_float(const char *key, float *value)
{
    char value_str[16];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0)
    {
        *value = strtof(value_str, NULL);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

aicam_result_t json_config_nvs_write_uint8(const char *key, uint8_t value)
{
    char value_str[4];
    snprintf(value_str, sizeof(value_str), "%u", value);
    int result = storage_nvs_write(NVS_USER, key, value_str, strlen(value_str) + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_uint8(const char *key, uint8_t *value)
{
    char value_str[4];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0)
    {
        *value = (uint8_t)strtoul(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

aicam_result_t json_config_nvs_write_bool(const char *key, aicam_bool_t value)
{
    const char *bool_str = (value == AICAM_TRUE) ? "1" : "0";
    int result = storage_nvs_write(NVS_USER, key, bool_str, strlen(bool_str) + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_bool(const char *key, aicam_bool_t *value)
{
    char value_str[2];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0)
    {
        *value = (strcmp(value_str, "1") == 0) ? AICAM_TRUE : AICAM_FALSE;
        return AICAM_OK;
    }
    return AICAM_ERROR;
}

aicam_result_t json_config_nvs_write_int32(const char *key, int32_t value)
{
    char value_str[12];
    snprintf(value_str, sizeof(value_str), "%ld", value);
    int result = storage_nvs_write(NVS_USER, key, value_str, strlen(value_str) + 1);
    return (result >= 0) ? AICAM_OK : AICAM_ERROR;
}

aicam_result_t json_config_nvs_read_int32(const char *key, int32_t *value)
{
    char value_str[12];
    int result = storage_nvs_read(NVS_USER, key, value_str, sizeof(value_str));
    if (result >= 0) {
        *value = (int32_t)strtol(value_str, NULL, 10);
        return AICAM_OK;
    }
    return AICAM_ERROR;
}