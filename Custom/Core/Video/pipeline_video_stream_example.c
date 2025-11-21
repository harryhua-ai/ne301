/**
 * @file pipeline_video_stream_example.c
 * @brief Complete Pipeline Video Stream Example
 * @details Demonstrates complete zero-copy video pipeline from camera to encoder
 */

#include "video_pipeline.h"
#include "video_frame_mgr.h"
#include "video_camera_node.h"
#include "video_encoder_node.h"
#include "video_ai_node.h"
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== Pipeline Configuration ==================== */

/**
 * @brief Pipeline configuration structure
 */
typedef struct {
    uint32_t width;                       // Video width
    uint32_t height;                      // Video height
    uint32_t fps;                         // Frame rate
    uint32_t duration_seconds;            // Stream duration in seconds
    uint32_t quality;                     // Encoding quality (0-100)
    aicam_bool_t enable_stats;            // Enable statistics logging
    aicam_bool_t enable_debug;            // Enable debug logging
} pipeline_config_t;

/**
 * @brief Pipeline statistics
 */
typedef struct {
    uint64_t total_frames_captured;       // Total frames captured by camera
    uint64_t total_frames_encoded;        // Total frames encoded
    uint64_t total_bytes_encoded;         // Total bytes encoded
    uint64_t pipeline_errors;             // Pipeline errors
    uint64_t start_time_ms;               // Pipeline start time
    uint64_t end_time_ms;                 // Pipeline end time
    uint32_t avg_fps;                     // Average FPS achieved
    uint32_t avg_bitrate_kbps;            // Average bitrate in kbps
} pipeline_stats_t;

/* ==================== Pipeline Context ==================== */

/**
 * @brief Pipeline context structure
 */
typedef struct {
    pipeline_config_t config;             // Pipeline configuration
    pipeline_stats_t stats;               // Pipeline statistics
    video_pipeline_t *pipeline;           // Video pipeline handle
    video_node_t *camera_node;            // Camera node handle
    video_node_t *encoder_node;           // Encoder node handle
    video_node_t *ai_node;                // AI node handle
    aicam_bool_t is_running;              // Pipeline running status
    aicam_bool_t is_initialized;          // Pipeline initialization status
} pipeline_context_t;

/* ==================== Global Pipeline Context ==================== */

static pipeline_context_t g_pipeline_ctx = {0};

/* ==================== Pipeline Event Callback ==================== */

/**
 * @brief Pipeline event callback function
 * @param event_type Event type
 * @param node_id Node ID that generated the event
 * @param event_data Event data
 * @param user_data User data pointer
 */
static void pipeline_event_callback(uint32_t event_type,
                                  uint32_t node_id,
                                  void *event_data,
                                  void *user_data)
{
    pipeline_context_t *ctx = (pipeline_context_t*)user_data;
    if (!ctx) return;
    
    switch (event_type) {
        case VIDEO_PIPELINE_EVENT_STARTED:
            LOG_CORE_INFO("Pipeline event: Pipeline started");
            break;
            
        case VIDEO_PIPELINE_EVENT_STOPPED:
            LOG_CORE_INFO("Pipeline event: Pipeline stopped");
            break;
            
        case VIDEO_PIPELINE_EVENT_ERROR:
            LOG_CORE_ERROR("Pipeline event: Pipeline error");
            ctx->stats.pipeline_errors++;
            break;
            
        case VIDEO_PIPELINE_EVENT_NODE_ADDED:
            LOG_CORE_INFO("Pipeline event: Node %d added", node_id);
            break;
            
        case VIDEO_PIPELINE_EVENT_CONNECTED:
            LOG_CORE_INFO("Pipeline event: Nodes connected");
            break;
            
        default:
            LOG_CORE_DEBUG("Pipeline event: Unknown event %d from node %d", event_type, node_id);
            break;
    }
}

/* ==================== Pipeline Management Functions ==================== */

/**
 * @brief Initialize pipeline configuration with default values
 * @param config Configuration structure to initialize
 */
void pipeline_get_default_config(pipeline_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(pipeline_config_t));
    config->width = 1280;
    config->height = 720;
    config->fps = 30;
    config->duration_seconds = 10;
    config->quality = 80;
    config->enable_stats = AICAM_TRUE;
    config->enable_debug = AICAM_FALSE;
}

/**
 * @brief Initialize video pipeline
 * @param config Pipeline configuration
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_init(const pipeline_config_t *config)
{
    if (!config) {
        LOG_CORE_ERROR("Invalid pipeline configuration");
        return AICAM_ERROR_INVALID_PARAM;
    }
    
    // Initialize pipeline context
    memset(&g_pipeline_ctx, 0, sizeof(pipeline_context_t));
    memcpy(&g_pipeline_ctx.config, config, sizeof(pipeline_config_t));
    
    // Initialize video pipeline system
    aicam_result_t result = video_pipeline_system_init();
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize video pipeline system: %d", result);
        return result;
    }
    
    // Create pipeline configuration
    video_pipeline_config_t pipeline_config = {
        .name = "ZeroCopyVideoPipeline",
        .max_nodes = 4,
        .max_connections = 4,
        .global_flow_mode = FLOW_MODE_PUSH,
        .auto_start = AICAM_FALSE,
        .event_callback = pipeline_event_callback,
        .user_data = &g_pipeline_ctx
    };
    
    // Create pipeline
    result = video_pipeline_create(&pipeline_config, &g_pipeline_ctx.pipeline);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to create video pipeline: %d", result);
        return result;
    }
    
    // Create camera node configuration
    video_camera_config_t camera_config;
    video_camera_get_default_config(&camera_config);
    camera_config.width = 1280;
    camera_config.height = 720;
    camera_config.fps = 30;
    camera_config.bpp = 2;
    camera_config.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;
    camera_config.ai_enabled = AICAM_FALSE;

       
    // pipe_param.width = 224;
    // pipe_param.height = 224;
    // pipe_param.fps = 30;
    // pipe_param.bpp = 3;
    // pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;

    // pipe_param.width = 1280;
    // pipe_param.height = 720;
    // pipe_param.fps = 30;
    // pipe_param.bpp = 2;
    // pipe_param.format = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1;

    //create ai node configuration
    video_ai_config_t ai_config;
    video_ai_get_default_config(&ai_config);
    ai_config.width = 1280;
    ai_config.height = 720;
    ai_config.fps = 30;
    ai_config.input_format = DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1;
    // ai_config.confidence_threshold = 60;
    // ai_config.max_detections = 32;
    ai_config.processing_interval = 1;
    ai_config.enabled = AICAM_FALSE;
    
    // Create encoder node configuration
    video_encoder_config_t encoder_config;
    video_encoder_get_default_config(&encoder_config);
    encoder_config.width = config->width;
    encoder_config.height = config->height;
    encoder_config.fps = config->fps;
    encoder_config.quality = config->quality;
    
    // Create standalone nodes first
    g_pipeline_ctx.camera_node = video_camera_node_create("ZeroCopyCamera", &camera_config);
    g_pipeline_ctx.encoder_node = video_encoder_node_create("ZeroCopyEncoder", &encoder_config);
    g_pipeline_ctx.ai_node = video_ai_node_create("ZeroCopyAI", &ai_config);
    
    if (!g_pipeline_ctx.camera_node || !g_pipeline_ctx.encoder_node || !g_pipeline_ctx.ai_node) {
        LOG_CORE_ERROR("Failed to create standalone nodes");
        return AICAM_ERROR_NO_MEMORY;
    }
    
    
    // Register standalone nodes with pipeline
    uint32_t camera_node_id, encoder_node_id, ai_node_id;
    result = video_pipeline_register_node(g_pipeline_ctx.pipeline, 
                                        g_pipeline_ctx.camera_node, 
                                        &camera_node_id);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to register camera node: %d", result);
        return result;
    }
    
    result = video_pipeline_register_node(g_pipeline_ctx.pipeline, 
                                        g_pipeline_ctx.encoder_node, 
                                        &encoder_node_id);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to register encoder node: %d", result);
        return result;
    }
    
    result = video_pipeline_register_node(g_pipeline_ctx.pipeline, 
                                        g_pipeline_ctx.ai_node, 
                                        &ai_node_id);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to register ai node: %d", result);
        return result;
    }
    
    // Connect camera to encoder
    result = video_pipeline_connect_nodes(g_pipeline_ctx.pipeline, 
                                        camera_node_id, 0, 
                                        ai_node_id, 0);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to connect camera to ai: %d", result);
        return result;
    }
    
    result = video_pipeline_connect_nodes(g_pipeline_ctx.pipeline, 
                                        ai_node_id, 0, 
                                        encoder_node_id, 0);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to connect ai to encoder: %d", result);
        return result;
    }
    
    g_pipeline_ctx.is_initialized = AICAM_TRUE;
    
    LOG_CORE_INFO("Pipeline initialized successfully: %dx%d@%dfps, quality=%d, duration=%ds",
                  config->width, config->height, config->fps, config->quality, config->duration_seconds);
    
    return AICAM_OK;
}

/**
 * @brief Start video pipeline
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_start(void)
{
    if (!g_pipeline_ctx.is_initialized) {
        LOG_CORE_ERROR("Pipeline not initialized");
        return AICAM_ERROR;
    }
    
    if (g_pipeline_ctx.is_running) {
        LOG_CORE_WARN("Pipeline already running");
        return AICAM_OK;
    }
    
    // Start pipeline
    aicam_result_t result = video_pipeline_start(g_pipeline_ctx.pipeline);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to start pipeline: %d", result);
        return result;
    }
    
    g_pipeline_ctx.is_running = AICAM_TRUE;
    g_pipeline_ctx.stats.start_time_ms = osKernelGetTickCount();
    
    LOG_CORE_INFO("Pipeline started successfully");
    LOG_CORE_INFO("Zero-copy video stream: Camera -> Encoder");
    LOG_CORE_INFO("Hardware buffers flow without any copying!");
    
    return AICAM_OK;
}

/**
 * @brief Stop video pipeline
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_stop()
{
    (void)argc;
    (void)argv;
    
    if (!g_pipeline_ctx.is_initialized) {
        LOG_CORE_ERROR("Pipeline not initialized");
        return AICAM_ERROR;
    }
    
    if (!g_pipeline_ctx.is_running) {
        LOG_CORE_WARN("Pipeline not running");
        return AICAM_OK;
    }
    
    // Stop pipeline
    aicam_result_t result = video_pipeline_stop(g_pipeline_ctx.pipeline);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to stop pipeline: %d", result);
        return result;
    }
    
    g_pipeline_ctx.is_running = AICAM_FALSE;
    g_pipeline_ctx.stats.end_time_ms = osKernelGetTickCount();
    
    LOG_CORE_INFO("Pipeline stopped successfully");
    
    return AICAM_OK;
}

/**
 * @brief Deinitialize video pipeline
 */
void pipeline_deinit(void)
{
    // video_pipeline_destroy internally calls video_pipeline_stop
    // So no need to call pipeline_stop first
    if (g_pipeline_ctx.pipeline) {
        video_pipeline_destroy(g_pipeline_ctx.pipeline);
        g_pipeline_ctx.pipeline = NULL;
    }
    
    video_pipeline_system_deinit();
    
    g_pipeline_ctx.is_initialized = AICAM_FALSE;
    g_pipeline_ctx.is_running = AICAM_FALSE;
    
    LOG_CORE_INFO("Pipeline deinitialized");
}

/**
 * @brief Get pipeline statistics
 * @param stats Statistics structure to fill
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_get_stats(pipeline_stats_t *stats)
{
    if (!stats) return AICAM_ERROR_INVALID_PARAM;
    
    if (!g_pipeline_ctx.is_initialized) {
        LOG_CORE_ERROR("Pipeline not initialized");
        return AICAM_ERROR;
    }
    
    // Get camera statistics
    video_node_stats_t camera_stats;
    if (g_pipeline_ctx.camera_node) {
        video_node_get_stats(g_pipeline_ctx.camera_node, &camera_stats);
        g_pipeline_ctx.stats.total_frames_captured = camera_stats.frames_processed;
    }
    
    // Get encoder statistics
    video_node_stats_t encoder_stats;
    if (g_pipeline_ctx.encoder_node) {
        video_node_get_stats(g_pipeline_ctx.encoder_node, &encoder_stats);
        g_pipeline_ctx.stats.total_frames_encoded = encoder_stats.frames_processed;
        g_pipeline_ctx.stats.total_bytes_encoded = encoder_stats.frames_processed;
    }
    
    // Calculate derived statistics
    if (g_pipeline_ctx.stats.end_time_ms > g_pipeline_ctx.stats.start_time_ms) {
        uint64_t duration_ms = g_pipeline_ctx.stats.end_time_ms - g_pipeline_ctx.stats.start_time_ms;
        if (duration_ms > 0) {
            g_pipeline_ctx.stats.avg_fps = (uint32_t)((g_pipeline_ctx.stats.total_frames_encoded * 1000) / duration_ms);
            g_pipeline_ctx.stats.avg_bitrate_kbps = (uint32_t)((g_pipeline_ctx.stats.total_bytes_encoded * 8) / duration_ms);
        }
    }
    
    memcpy(stats, &g_pipeline_ctx.stats, sizeof(pipeline_stats_t));
    return AICAM_OK;
}

/**
 * @brief Print pipeline statistics
 */
void pipeline_print_stats(void)
{
    pipeline_stats_t stats;
    aicam_result_t result = pipeline_get_stats(&stats);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to get pipeline statistics");
        return;
    }
    
    LOG_CORE_INFO("=== Pipeline Statistics ===");
    LOG_CORE_INFO("Total frames captured: %llu", stats.total_frames_captured);
    LOG_CORE_INFO("Total frames encoded: %llu", stats.total_frames_encoded);
    LOG_CORE_INFO("Total bytes encoded: %llu", stats.total_bytes_encoded);
    LOG_CORE_INFO("Pipeline errors: %llu", stats.pipeline_errors);
    LOG_CORE_INFO("Average FPS: %d", stats.avg_fps);
    LOG_CORE_INFO("Average bitrate: %d kbps", stats.avg_bitrate_kbps);
    
    if (stats.end_time_ms > stats.start_time_ms) {
        uint64_t duration_ms = stats.end_time_ms - stats.start_time_ms;
        LOG_CORE_INFO("Stream duration: %llu ms (%.2f seconds)", duration_ms, duration_ms / 1000.0f);
    }
    
    LOG_CORE_INFO("==========================");
}

/* ==================== Pipeline Control Functions ==================== */

/**
 * @brief Check if pipeline is running
 * @return Running status
 */
aicam_bool_t pipeline_is_running(void)
{
    return g_pipeline_ctx.is_running;
}

/**
 * @brief Check if pipeline is initialized
 * @return Initialization status
 */
aicam_bool_t pipeline_is_initialized(void)
{
    return g_pipeline_ctx.is_initialized;
}

/**
 * @brief Set pipeline configuration
 * @param config New configuration
 * @return AICAM_OK on success
 */
aicam_result_t pipeline_set_config(const pipeline_config_t *config)
{
    if (!config) return AICAM_ERROR_INVALID_PARAM;
    
    if (g_pipeline_ctx.is_running) {
        LOG_CORE_ERROR("Cannot change configuration while pipeline is running");
        return AICAM_ERROR;
    }
    
    memcpy(&g_pipeline_ctx.config, config, sizeof(pipeline_config_t));
    
    // Update camera configuration
    if (g_pipeline_ctx.camera_node) {
        video_camera_config_t camera_config;
        video_camera_get_default_config(&camera_config);
        camera_config.width = config->width;
        camera_config.height = config->height;
        camera_config.fps = config->fps;
        
        // Update private data with new config
        video_camera_node_data_t *camera_data = (video_camera_node_data_t*)video_node_get_private_data(g_pipeline_ctx.camera_node);
        if (camera_data) {
            memcpy(&camera_data->config, &camera_config, sizeof(video_camera_config_t));
        }
    }
    
    // Update encoder configuration
    if (g_pipeline_ctx.encoder_node) {
        video_encoder_config_t encoder_config;
        video_encoder_get_default_config(&encoder_config);
        encoder_config.width = config->width;
        encoder_config.height = config->height;
        encoder_config.fps = config->fps;
        encoder_config.quality = config->quality;
        
        // Update private data with new config
        video_encoder_node_data_t *encoder_data = (video_encoder_node_data_t*)video_node_get_private_data(g_pipeline_ctx.encoder_node);
        if (encoder_data) {
            memcpy(&encoder_data->config, &encoder_config, sizeof(video_encoder_config_t));
        }
    }
    
    LOG_CORE_INFO("Pipeline configuration updated: %dx%d@%dfps, quality=%d",
                  config->width, config->height, config->fps, config->quality);
    
    return AICAM_OK;
}

/* ==================== Main Pipeline Example ==================== */

/**
 * @brief Complete pipeline video stream example
 */
void complete_pipeline_video_stream_example(void)
{
    LOG_CORE_INFO("=== Complete Pipeline Video Stream Example ===");
    
    // Initialize pipeline configuration
    pipeline_config_t config;
    pipeline_get_default_config(&config);
    config.width = 1280;
    config.height = 720;
    config.fps = 30;
    config.duration_seconds = 10;
    config.quality = 80;
    config.enable_stats = AICAM_TRUE;
    config.enable_debug = AICAM_TRUE;
    
    // Initialize pipeline
    aicam_result_t result = pipeline_init(&config);
    if (result != AICAM_OK) {
        LOG_CORE_ERROR("Failed to initialize pipeline: %d", result);
        return;
    }
    
    // Start pipeline
    // result = pipeline_start();
    // if (result != AICAM_OK) {
    //     LOG_CORE_ERROR("Failed to start pipeline: %d", result);
    //     pipeline_deinit();
    //     return;
    // }
    
    // LOG_CORE_INFO("Pipeline started successfully");
    // LOG_CORE_INFO("Streaming for %d seconds...", config.duration_seconds);
    
    // Monitor pipeline for specified duration
    // uint32_t elapsed_seconds = 0;
    // while (elapsed_seconds < config.duration_seconds && pipeline_is_running()) {
    //     osDelay(1000);  // Wait 1 second
    //     elapsed_seconds++;
        
    //     // Print periodic statistics
    //     if (config.enable_stats && (elapsed_seconds % 2 == 0)) {
    //         pipeline_stats_t stats;
    //         if (pipeline_get_stats(&stats) == AICAM_OK) {
    //             LOG_CORE_INFO("Progress: %d/%ds, Frames: %d captured, %d encoded",
    //                           elapsed_seconds, config.duration_seconds,
    //                           stats.total_frames_captured, stats.total_frames_encoded);
    //         }
    //     }
    // }
    
    // Stop pipeline
    // result = pipeline_stop();
    // if (result != AICAM_OK) {
    //     LOG_CORE_ERROR("Failed to stop pipeline: %d", result);
    // }
    
    // Print final statistics
    // pipeline_print_stats();
    
    // Deinitialize pipeline
   //  pipeline_deinit();
    
    LOG_CORE_INFO("Complete pipeline video stream example completed");
}

/**
 * @brief High-performance pipeline example with different configurations
 */
void high_performance_pipeline_example(void)
{
    LOG_CORE_INFO("=== High-Performance Pipeline Example ===");
    
    // Test different configurations
    pipeline_config_t configs[] = {
        {1280, 720, 30, 5, 80, AICAM_TRUE, AICAM_FALSE},   // 720p@30fps
        {1920, 1080, 30, 5, 85, AICAM_TRUE, AICAM_FALSE},  // 1080p@30fps
        {1280, 720, 60, 5, 90, AICAM_TRUE, AICAM_FALSE},   // 720p@60fps
    };
    
    const char* config_names[] = {"720p@30fps", "1080p@30fps", "720p@60fps"};
    
    for (int i = 0; i < 3; i++) {
        LOG_CORE_INFO("Testing configuration: %s", config_names[i]);
        
        // Initialize pipeline
        aicam_result_t result = pipeline_init(&configs[i]);
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to initialize pipeline for %s", config_names[i]);
            continue;
        }
        
        // Start pipeline
        result = pipeline_start();
        if (result != AICAM_OK) {
            LOG_CORE_ERROR("Failed to start pipeline for %s", config_names[i]);
            pipeline_deinit();
            continue;
        }
        
        // Run for specified duration
        uint32_t elapsed_seconds = 0;
        while (elapsed_seconds < configs[i].duration_seconds && pipeline_is_running()) {
            osDelay(1000);
            elapsed_seconds++;
        }
        
        // Stop pipeline
        pipeline_stop();
        
        // Print statistics
        LOG_CORE_INFO("Results for %s:", config_names[i]);
        pipeline_print_stats();
        
        // Deinitialize pipeline
        pipeline_deinit();
        
        LOG_CORE_INFO("Configuration %s completed", config_names[i]);
        
        // Wait between tests
        if (i < 2) {
            osDelay(2000);
        }
    }
    
    LOG_CORE_INFO("High-performance pipeline example completed");
}

void pipeline_stop_deinit(void)
{
    //pipeline_stop();
    pipeline_deinit();
}

/* ==================== Main Example Function ==================== */

/**
 * @brief Run all pipeline examples
 */
void run_pipeline_examples(void)
{
    LOG_CORE_INFO("Starting pipeline video stream examples...");
    
    // Run complete pipeline example
    complete_pipeline_video_stream_example();
    
    // Wait between examples
    osDelay(3000);
    
    // Run high-performance pipeline example
    high_performance_pipeline_example();
    
    LOG_CORE_INFO("All pipeline examples completed");
}


debug_cmd_reg_t pipeline_cmd_table[] = {
    {"complete_pipeline_video_stream_example", "Run complete pipeline video stream example", complete_pipeline_video_stream_example},
    {"high_performance_pipeline_example", "Run high-performance pipeline example", high_performance_pipeline_example},
    {"run_pipeline_examples", "Run all pipeline examples", run_pipeline_examples},
    {"pipeline_stop", "Stop pipeline", pipeline_stop},
    {"pipeline_start", "Start pipeline", pipeline_start},
};

/**
 * @brief Register unified pipeline CLI commands
 */
void pipeline_video_stream_example_register_commands(void)
{
   for (int i = 0; i < (int)(sizeof(pipeline_cmd_table) / sizeof(pipeline_cmd_table[0])); i++) {
        debug_register_commands(&pipeline_cmd_table[i], 1);
    }
}
