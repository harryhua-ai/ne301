export default class FixtureH264Player {
  private callback: (message: { t: string }) => void
  private video: HTMLVideoElement | null = null
  private canvas: HTMLCanvasElement | null = null
  private stream: MediaStream | null = null
  private socket: WebSocket | null = null
  private animation = 0

  constructor(callback: (message: { t: string }) => void) {
    this.callback = callback
  }

  initPlayer(video: HTMLVideoElement) {
    this.video = video
    return this
  }

  start(url: string) {
    this.socket?.close()
    this.socket = new WebSocket(url)
    this.socket.addEventListener('open', () => {
      window.__fixtureSocketOpen = true
      window.dispatchEvent(new Event('fixture-websocket-open'))
    }, { once: true })
    this.socket.addEventListener('error', () => this.callback({ t: 'mseError' }), { once: true })
    this.canvas = document.createElement('canvas')
    this.canvas.width = 320
    this.canvas.height = 180
    this.stream = this.canvas.captureStream(12)
    if (this.video) {
      this.video.srcObject = this.stream
      void this.video.play().catch(() => undefined)
    }
    const context = this.canvas.getContext('2d')
    const draw = () => {
      if (!context || !this.canvas) return
      context.fillStyle = '#13213b'
      context.fillRect(0, 0, this.canvas.width, this.canvas.height)
      context.fillStyle = '#2f80ed'
      context.fillRect((Date.now() / 12) % 300, 48, 20, 76)
      context.fillStyle = '#eab308'
      context.fillRect(0, 142, 320, 4)
      this.animation = requestAnimationFrame(draw)
    }
    draw()
    return this
  }

  hardRestart() {
    if (this.socket?.readyState === WebSocket.OPEN) this.socket.close()
    return this.start(`${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/stream`)
  }

  resetStartState() {
    return this
  }

  destroy() {
    cancelAnimationFrame(this.animation)
    this.socket?.close()
    this.stream?.getTracks().forEach(track => track.stop())
    if (this.video) {
      this.video.pause()
      this.video.srcObject = null
    }
    this.canvas = null
    this.stream = null
    this.socket = null
  }
}
