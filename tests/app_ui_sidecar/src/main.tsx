import { render } from 'preact'
import PeopleCountingPage from '@/pages/peopleCounting/index'
import '@/styles/index.css'
import './fixture.css'
import { installFixtureDebug, requestCapability } from '../adapters/bridge'

const root = document.querySelector('#root')
if (!root) throw new Error('root element missing')

window.__fixtureBuildInfo = {
  htmlBuild: document.querySelector('meta[name="sidecar-build"]')?.getAttribute('content') || 'unknown',
  assetBuild: 'pending'
}

void fetch('/build-label', { cache: 'no-store' })
  .then(response => response.json())
  .then(result => { window.__fixtureBuildInfo.assetBuild = result.label })

render(<PeopleCountingPage />, root)
installFixtureDebug()

const probe = async () => {
  const workerPromise = new Promise(resolve => {
    const worker = new Worker(new URL('./probe-worker.ts', import.meta.url), { type: 'module' })
    const timeout = setTimeout(() => {
      worker.terminate()
      resolve(false)
    }, 2500)
    worker.addEventListener('message', event => {
      clearTimeout(timeout)
      worker.terminate()
      resolve(event.data?.ready === true)
    }, { once: true })
    worker.postMessage({ type: 'probe' })
  })
  const video = document.querySelector('video#pcVideoPlayer')
  const videoPromise = new Promise(resolve => {
    if (!video) return resolve(false)
    if (video.readyState >= 1 && video.videoWidth > 0) return resolve(true)
    video.addEventListener('loadedmetadata', () => resolve(video.videoWidth > 0), { once: true })
    setTimeout(() => resolve(false), 2500)
  })
  const mediaSource = typeof MediaSource !== 'undefined' && MediaSource.isTypeSupported('video/mp4; codecs="avc1.42E01E"')
  const canvasCapture = typeof HTMLCanvasElement.prototype.captureStream === 'function'
  const worker = await workerPromise
  const videoReady = await videoPromise
  const socketWait = new Promise(resolve => {
    if (window.__fixtureSocketOpen) return resolve(true)
    window.addEventListener('fixture-websocket-open', () => resolve(true), { once: true })
    setTimeout(() => resolve(false), 2500)
  })
  const webSocketOpen = await socketWait
  window.__fixtureCapabilities = { mediaSource, canvasCapture, worker, videoReady, webSocketOpen }
}

void probe()
void requestCapability('peopleCounting.config.read')
