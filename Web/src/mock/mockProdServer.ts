import { createProdMockServer } from 'vite-plugin-mock/client'
// import deviceToolMock from './device-tool-mock'
// import modelVerificationMock from './model-verification-mock'
// import loginMock from './login-mock'

export function setupProdMockServer() {
    createProdMockServer([])
  }