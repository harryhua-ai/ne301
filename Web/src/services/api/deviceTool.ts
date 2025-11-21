import request from '../request'

export interface WorkModeSwitchReq {
    mode:  'image' | 'video_stream'
}

export interface AiParams {
   nms_threshold: number,
    confidence_threshold: number
}
export interface VideoStreamPushReq {
    enabled: boolean,
    server_url?: string,
}
export interface ImageTriggerReq {
    io_trigger?: boolean,
    pir_trigger?: boolean,
    timer_trigger?: boolean
}
const deviceTool = {
    // Whether to enable video stream
    startVideoStreamReq: () => request.post('api/v1/ai/pipeline/start'),
    stopVideoStreamReq: () => request.post('api/v1/ai/pipeline/stop'),

    // Get work mode status
    getWorkModeStatusReq: () => request.get('/api/v1/work-mode/status'),
    // Switch work mode
    switchWorkModeReq: (data: WorkModeSwitchReq) => request.post('/api/v1/work-mode/switch', data),
   
    // Configure video stream push
    configVideoStreamPushReq: (data: VideoStreamPushReq) => request.post('/api/v1/work-mode/video-stream/config', data),
    
    // Configure image mode trigger
    configTriggerConfigReq: (data: ImageTriggerReq) => request.post('/api/v1/work-mode/triggers', data),

    getTriggerConfigReq: () => request.get('/api/v1/work-mode/triggers'),
    
    // Switch power mode
    switchPowerModeReq: (data: { mode: string }) => request.post('api/v1/power-mode/switch', data),
    getPowerModeReq: () => request.get('api/v1/power-mode/status'),
    // -----------------
    
    // Get AI status
    getAiStatusReq: () => request.get('/api/v1/ai/status'),
    toggleAiReq: (data: { ai_enabled: boolean }) => request.post('/api/v1/ai/toggle', data),
    getTriggerMethodReq: () => request.get('/api/v1/trigger-method/status'),

    // AI parameters
    getAiParamsReq: () => request.get('/api/v1/ai/params'),
    setAiParamsReq: (data: AiParams) => request.post('/api/v1/ai/params', data),

}

export default deviceTool;