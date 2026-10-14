export const getWebSocketUrl = (path: string) => `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}${path}`
