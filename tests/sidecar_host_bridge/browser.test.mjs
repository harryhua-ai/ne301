import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import test from 'node:test'
import { chromium } from 'playwright-core'
import { homedir } from 'node:os'

const chromeExecutable = path.join(
  homedir(),
  'Library/Caches/ms-playwright/chromium-1243/chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing'
)
const launch = () => chromium.launch({ headless: true, executablePath: chromeExecutable })
import {
  createAppServer,
  createHostServer,
  setBehavior,
  submitSnapshot,
  currentGeneration,
  grantCount
} from './server.mjs'

const distRoot = path.join(path.dirname(new URL(import.meta.url).pathname), 'dist-host')
const appRoot = path.join(distRoot, 'app-site')

const listen = (server, host) =>
  new Promise((resolve, reject) => {
    server.once('error', reject)
    server.listen(0, host, () => resolve(server.address().port))
  })

const closeServer = (server) =>
  new Promise((resolve) => {
    if (!server?.listening) return resolve()
    server.close(() => resolve())
  })

async function startStack({ mode = 'normal', timeoutMs } = {}) {
  const appServer = createAppServer({ appRoot, allowedHostOrigin: '*', hostOriginForFrame: '' })
  const appPort = await listen(appServer, 'localhost')
  const appOrigin = `http://localhost:${appPort}`
  const hostServer = createHostServer({ distRoot, getAppSiteOrigin: () => appOrigin })
  const hostPort = await listen(hostServer, '127.0.0.1')
  const hostOrigin = `http://127.0.0.1:${hostPort}`
  hostServer.closeAllConnections?.()
  const timeoutParam = timeoutMs ? `&timeoutMs=${timeoutMs}` : ''
  const hostUrl = `${hostOrigin}/?mode=${mode}${timeoutParam}`
  return { hostServer, appServer, hostOrigin, appOrigin, hostUrl, appOriginOverride: appOrigin }
}

test.describe = test

test('sidecar bridge end-to-end happy path with signed assets, workers and typed results', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot(JSON.stringify({ counters: { in: 3 } }))
    setBehavior({ failSnapshot: false, holdConfigApply: false, staleReceipt: false })
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    await page.waitForFunction(
      () => document.querySelector('[data-testid="host-status"]')?.textContent === 'granted',
      undefined,
      { timeout: 15000 }
    )
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="app-status"]').filter({ hasText: 'granted' }).waitFor({ timeout: 15000 })
    await frame
      .locator('[data-testid="app-workers"]')
      .filter({ hasText: '2/2' })
      .waitFor({ timeout: 15000 })
    const workers = await frame.locator('[data-testid="app-workers"]').textContent()
    assert.equal(workers, '2/2', 'dual blob workers run inside the sandboxed cross-site frame')
    await frame
      .locator('[data-testid="app-snapshot"]')
      .filter({ hasText: '"outcome":"APPLIED"' })
      .waitFor({ timeout: 15000 })
    const snapshot = JSON.parse(await frame.locator('[data-testid="app-snapshot"]').textContent())
    assert.equal(snapshot.freshness, 'fresh')
    if (snapshot.payload !== undefined) {
      assert.deepEqual(snapshot.payload, { counters: { in: 3 } })
    }

    const sandbox = await page.getAttribute('[data-testid="host-frame"]', 'sandbox')
    assert.equal(sandbox, 'allow-scripts allow-same-origin allow-downloads')
    const frameSrc = await page.getAttribute('[data-testid="host-frame"]', 'src')
    assert.ok(frameSrc.startsWith(stack.appOrigin), 'frame src points at the distinct app site')

    const asset = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/app.min.js`
    )
    assert.equal(asset.status(), 200)
    assert.match(asset.headers()['cache-control'] || '', /no-store/)

    const missing = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/unknown.js`
    )
    assert.equal(missing.status(), 404)
    const traversal = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/..%2findex.html`
    )
    assert.equal(traversal.status(), 404)
    const staleGeneration = await page.request.get(`${stack.hostOrigin}/appsite/g1/index.html`)
    assert.equal(staleGeneration.status(), 404)
    const tampered = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/app.min.js?tamper=yes`
    )
    assert.equal(tampered.status(), 404)
    assert.equal(grantCount() >= 1, true)
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('hostile forged iframe window message is rejected and cannot touch the trusted session', async () => {
  const stack = await startStack({ mode: 'forged-message' })
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    await page.waitForFunction(
      () => document.querySelector('[data-testid="host-status"]')?.textContent === 'granted',
      undefined,
      { timeout: 15000 }
    )
    await page
      .locator('[data-testid="host-forged-rejected"]')
      .filter({ hasText: 'ignored-window-message' })
      .waitFor({ timeout: 15000 })
    const status = await page.textContent('[data-testid="host-status"]')
    assert.equal(status, 'granted', 'forged traffic never changes the trusted session state')
    const frameCount = await page.locator('[data-testid="host-frame"]').count()
    assert.equal(frameCount, 1, 'the trusted frame stays because the forgery never touched the port')
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('replayed request_id fails closed with REJECTED replay', async () => {
  const stack = await startStack({ mode: 'replay' })
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame
      .locator('[data-testid="app-status"]')
      .filter({ hasText: 'granted' })
      .waitFor({ timeout: 15000 })
    await page.waitForFunction(
      () => String((window).lastResult?.reason ?? '') === 'REPLAY',
      undefined,
      { timeout: 15000 }
    )
    const outcome = await page.evaluate(() => window.lastResult.outcome)
    assert.equal(outcome, 'REJECTED')
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('unreviewed capabilities stay unavailable and the save control never fakes success', async () => {
  const stack = await startStack({ mode: 'policy-save' })
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame
      .locator('[data-testid="app-policy-save"]')
      .filter({ hasText: 'UNAVAILABLE' })
      .waitFor({ timeout: 15000 })
    const saved = JSON.parse(await frame.locator('[data-testid="app-policy-save"]').textContent())
    assert.equal(saved.outcome, 'UNAVAILABLE')
    assert.equal(saved.reason, 'CAPABILITY_NOT_REVIEWED')
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('destructive counters.reset fails closed without an explicit admin confirm token', async () => {
  const stack = await startStack({ mode: 'config-apply-unconfirmed' })
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame
      .locator('[data-testid="app-counters-reset"]')
      .filter({ hasText: 'REJECTED' })
      .waitFor({ timeout: 15000 })
    const result = JSON.parse(await frame.locator('[data-testid="app-counters-reset"]').textContent())
    assert.equal(result.reason, 'ADMIN_CONFIRM_REQUIRED')
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('config.apply only reports APPLIED with a trusted final result and stays UNKNOWN on hold', async () => {
  const stack = await startStack({ mode: 'config-apply', timeoutMs: 1500 })
  const browser = await launch()
  try {
    const page = await browser.newPage()
    setBehavior({ holdConfigApply: true })
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame
      .locator('[data-testid="app-config-apply"]')
      .filter({ hasText: 'UNKNOWN' })
      .waitFor({ timeout: 20000 })
    const held = JSON.parse(await frame.locator('[data-testid="app-config-apply"]').textContent())
    assert.equal(held.reason, 'ACK_TIMEOUT')

    setBehavior({ holdConfigApply: false })
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}&mode=config-apply`)
    const frame2 = page.frameLocator('[data-testid="host-frame"]')
    await frame2
      .locator('[data-testid="app-config-apply"]')
      .filter({ hasText: 'APPLIED' })
      .waitFor({ timeout: 20000 })
    const applied = JSON.parse(await frame2.locator('[data-testid="app-config-apply"]').textContent())
    assert.equal(applied.outcome, 'APPLIED')
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('generation switch mid-session revokes the grant and tears down the old frame', async () => {
  const stack = await startStack({ mode: 'generation-switch' })
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    await page.waitForFunction(
      () => {
        const status = document.querySelector('[data-testid="host-status"]')?.textContent
        return status === 'granted' || status === 'revoked'
      },
      undefined,
      { timeout: 15000 }
    )
    await page
      .waitForFunction(
        () => document.querySelector('[data-testid="host-status"]')?.textContent === 'revoked',
        undefined,
        { timeout: 20000 }
      )
    const reason = await page.textContent('[data-testid="host-revoked"]')
    assert.match(String(reason), /IDENTITY_OR_GENERATION_CHANGED|GENERATION/)
    const frameCount = await page.locator('[data-testid="host-frame"]').count()
    assert.equal(frameCount, 0, 'old-generation frame is destroyed')
    const staleAsset = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration() - 1}/index.html`
    )
    assert.equal(staleAsset.status(), 404)
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('same-site app origin is refused by the host gate before any iframe is created', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.hostOrigin)}`)
    await page.waitForFunction(
      () => document.querySelector('[data-testid="host-status"]')?.textContent === 'blocked-same-site',
      undefined,
      { timeout: 15000 }
    )
    const frameCount = await page.locator('[data-testid="host-frame"]').count()
    assert.equal(frameCount, 0)
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('ACK timeout yields UNKNOWN without optimistic commit and never reports APPLIED', async () => {
  const stack = await startStack({ mode: 'config-apply', timeoutMs: 800 })
  const browser = await launch()
  try {
    setBehavior({ holdConfigApply: true })
    const page = await browser.newPage()
    await page.goto(`${stack.hostUrl}&appOrigin=${encodeURIComponent(stack.appOriginOverride)}`)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame
      .locator('[data-testid="app-config-apply"]')
      .filter({ hasText: 'UNKNOWN' })
      .waitFor({ timeout: 20000 })
    const held = JSON.parse(await frame.locator('[data-testid="app-config-apply"]').textContent())
    assert.equal(held.outcome, 'UNKNOWN')
    assert.equal(held.reason, 'ACK_TIMEOUT')
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})
