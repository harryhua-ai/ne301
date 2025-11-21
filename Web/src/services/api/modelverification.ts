import request from '../request'

export interface InferenceReq {
  image_id: string,
  model_name?: string
}
export interface InferenceImageReq {
  ai_image: File,
  draw_image: File,
  ai_image_width: string,
  ai_image_height: string,
  ai_image_quality: string,
  draw_image_width: string,
  draw_image_height: string,
  draw_image_quality: string,
}

const modelVerification = {
  getInference: (data: InferenceReq) => request.post('/api/modelVerification/inference', data),
  getInferenceExample: () => request.get('/api/modelVerification/inferenceExample'),
  inferenceImageReq: (data: InferenceImageReq) => request.post('/api/v1/model/validation/upload', data),
}

export default modelVerification