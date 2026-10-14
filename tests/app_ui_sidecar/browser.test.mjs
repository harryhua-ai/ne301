import assert from 'node:assert/strict'
import test from 'node:test'
import { startFixture } from './fixture-runtime.mjs'

const waitForCapability = async (runtime, capability) => {
  await runtime.page.waitForFunction(name => window.__hostAudit.accepted.some(item => item.capability === name), capability, { timeout: 5000 })
}

const waitForRejection = async (runtime, reason, afterCount = 0) => {
  await runtime.page.waitForFunction(
    ({ name, count }) => window.__hostAudit.rejected.slice(count).some(item => item.reason === name),
    { name: reason, count: afterCount },
    { timeout: 3000 }
  )
  return runtime.page.evaluate(name => window.__hostAudit.rejected.filter(item => item.reason === name).at(-1), reason)
}

test('isolated signed sidecar browser feasibility probe', async t => {
  const runtime = await startFixture()
  try {
    await t.test('renders the four historical people-counting UI components in Chrome', async () => {
      assert.notEqual(runtime.hostOrigin, runtime.oldOrigin)
      const frame = runtime.sidecarFrame(runtime.oldOrigin)
      await frame.waitForFunction(() => window.__fixtureBuildInfo?.htmlBuild === 'v1')
      await frame.getByText('Total In', { exact: true }).waitFor()
      await frame.getByText('Counting Line', { exact: true }).waitFor()
      await frame.getByRole('button', { name: 'Save', exact: true }).waitFor()
      await frame.locator('video#pcVideoPlayer').waitFor()
      await runtime.captureEvidence()
    })

    await t.test('blocks host DOM and host cookie reads across the sandboxed origin boundary', async () => {
      const frame = runtime.sidecarFrame(runtime.oldOrigin)
      const result = await frame.evaluate(() => {
        let dom = 'readable'
        let cookie = 'readable'
        try { window.parent.document.querySelector('#host-secret') } catch (error) { dom = error.name }
        try { window.parent.document.cookie } catch (error) { cookie = error.name }
        return { dom, cookie, origin: location.origin }
      })
      assert.equal(result.dom, 'SecurityError')
      assert.equal(result.cookie, 'SecurityError')
      assert.equal(result.origin, runtime.oldOrigin)
    })

    await t.test('keeps native access behind the typed capability allowlist and validates payloads', async () => {
      const frame = runtime.sidecarFrame(runtime.oldOrigin)
      await waitForCapability(runtime, 'peopleCounting.config.read')
      await waitForCapability(runtime, 'peopleCounting.stats.read')
      const deniedCapability = await frame.evaluate(async () => {
        try {
          await window.__fixtureDebug.requestUnsafe('device.reboot', {})
          return 'accepted'
        } catch (error) {
          return error.code
        }
      })
      assert.equal(deniedCapability, 'CAPABILITY_DENIED')
      const deniedPayload = await frame.evaluate(async () => {
        try {
          await window.__fixtureDebug.requestUnsafe('peopleCounting.config.write', { conf_threshold_permille: 2000 })
          return 'accepted'
        } catch (error) {
          return error.code
        }
      })
      assert.equal(deniedPayload, 'CAPABILITY_PAYLOAD_INVALID')
      const audit = await runtime.audit()
      assert.equal(audit.config.conf_threshold_permille, 300)
    })

    await t.test('rejects forged messages from the host window and a sibling frame', async () => {
      const initialCount = (await runtime.audit()).rejected.length
      await runtime.page.evaluate(() => {
        const audit = window.__hostAudit
        window.postMessage({ type: 'request', id: 'forged-host', nonce: audit.nonce, capability: 'peopleCounting.stats.reset', payload: {} }, location.origin)
      })
      const hostForge = await waitForRejection(runtime, 'source', initialCount)
      assert.equal(hostForge.requestId, 'forged-host')
      const siblingCount = (await runtime.audit()).rejected.length
      const sibling = await runtime.page.evaluate(async () => {
        const audit = window.__hostAudit
        const relay = document.createElement('iframe')
        relay.src = `${audit.expectedOrigin}/sibling.html`
        document.body.appendChild(relay)
        await new Promise(resolve => relay.addEventListener('load', resolve, { once: true }))
        relay.contentWindow.postMessage({
          type: 'relay',
          hostOrigin: location.origin,
          message: { type: 'request', id: 'forged-sibling', nonce: audit.nonce, capability: 'peopleCounting.stats.reset', payload: {} }
        }, audit.expectedOrigin)
        await new Promise(resolve => setTimeout(resolve, 150))
        relay.remove()
      })
      assert.equal(sibling, undefined)
      const siblingForge = await waitForRejection(runtime, 'source', siblingCount)
      assert.equal(siblingForge.requestId, 'forged-sibling')
    })

    await t.test('rejects a correct frame source when its origin no longer matches the binding', async () => {
      const frame = runtime.sidecarFrame(runtime.oldOrigin)
      const before = (await runtime.audit()).rejected.length
      await runtime.page.evaluate(origin => window.__hostTest.setExpectedOrigin(origin), runtime.attackerOrigin)
      const result = await frame.evaluate(async () => {
        try {
          await window.__fixtureDebug.requestUnsafe('peopleCounting.config.read', {})
          return 'accepted'
        } catch (error) {
          return error.code
        }
      })
      const rejected = await waitForRejection(runtime, 'origin', before)
      assert.equal(rejected.requestId.startsWith('req-'), true)
      assert.equal(result, 'REQUEST_TIMEOUT')
      await runtime.page.evaluate(origin => window.__hostTest.setExpectedOrigin(origin), runtime.oldOrigin)
    })

    await t.test('rejects the prior nonce after iframe unload, reload, and origin rebind', async () => {
      const oldNonce = await runtime.page.evaluate(() => window.__hostAudit.nonce)
      const oldGeneration = (await runtime.audit()).generation
      await runtime.switchSidecar(runtime.newOrigin, 'v2')
      const nextFrame = runtime.sidecarFrame(runtime.newOrigin)
      await nextFrame.waitForFunction(() => window.__fixtureBuildInfo?.htmlBuild === 'v2')
      await runtime.page.waitForFunction(value => window.__hostAudit.generation > value && window.__hostAudit.nonce, oldGeneration)
      const nextNonce = await runtime.page.evaluate(() => window.__hostAudit.nonce)
      assert.notEqual(nextNonce, oldNonce)
      const before = (await runtime.audit()).rejected.length
      await nextFrame.evaluate(({ nonce, hostOrigin }) => {
        window.parent.postMessage({ type: 'request', id: 'stale-unload', nonce, capability: 'peopleCounting.stats.reset', payload: {} }, hostOrigin)
      }, { nonce: oldNonce, hostOrigin: runtime.hostOrigin })
      const stale = await waitForRejection(runtime, 'nonce', before)
      assert.equal(stale.requestId, 'stale-unload')
    })

    await t.test('shows that no-store does not defeat a same-origin stale service worker and a new origin does', async () => {
      const frame = runtime.sidecarFrame(runtime.newOrigin)
      await frame.waitForFunction(() => window.__fixtureBuildInfo?.assetBuild === 'v2')
      const state = await frame.evaluate(() => ({
        htmlBuild: window.__fixtureBuildInfo.htmlBuild,
        assetBuild: window.__fixtureBuildInfo.assetBuild,
        controlled: Boolean(navigator.serviceWorker?.controller)
      }))
      assert.deepEqual(state, { htmlBuild: 'v2', assetBuild: 'v2', controlled: false })
      assert.equal(runtime.cacheProbe.sameOriginLabel, 'v1')
      assert.equal(runtime.cacheProbe.newOriginLabel, 'v2')
    })

    await t.test('keeps video, canvas line overlay, MediaSource, WebSocket, and Worker features available', async () => {
      const frame = runtime.sidecarFrame(runtime.newOrigin)
      const video = frame.locator('video#pcVideoPlayer')
      await video.waitFor({ state: 'visible' })
      await frame.waitForFunction(() => window.__fixtureCapabilities?.worker === true && window.__fixtureCapabilities?.webSocketOpen === true)
      const caps = await frame.evaluate(() => window.__fixtureCapabilities)
      assert.equal(caps.mediaSource, true)
      assert.equal(caps.canvasCapture, true)
      assert.equal(caps.worker, true)
      assert.equal(caps.webSocketOpen, true)
      assert.equal(caps.videoReady, true)
      await frame.getByRole('button', { name: 'Edit Line', exact: true }).click()
      await video.click({ position: { x: 70, y: 50 } })
      await video.click({ position: { x: 260, y: 150 } })
      await frame.locator('svg line').first().waitFor()
      await frame.getByRole('button', { name: 'Save', exact: true }).click()
      await waitForCapability(runtime, 'peopleCounting.config.write')
      const audit = await runtime.audit()
      assert.ok(audit.config.line_x1_permille !== audit.config.line_x2_permille)
    })

    await t.test('blocks top-level navigation and rejects a third-party iframe by CSP', async () => {
      const frame = runtime.sidecarFrame(runtime.newOrigin)
      const pageUrl = runtime.page.url()
      const topNavigation = await frame.evaluate(() => {
        try {
          window.top.location.href = `${window.__fixtureAttackerOrigin}/`
          return 'attempted'
        } catch (error) {
          return error.name
        }
      })
      await runtime.page.waitForTimeout(250)
      assert.ok(['attempted', 'SecurityError', 'NotAllowedError'].includes(topNavigation))
      assert.equal(runtime.page.url(), pageUrl)
      const outsider = await runtime.browser.newPage()
      const messages = []
      outsider.on('console', message => messages.push(message.text()))
      await outsider.goto(`${runtime.attackerOrigin}/embed.html`)
      await outsider.waitForTimeout(350)
      assert.ok(messages.some(message => /frame|ancestor|content security/i.test(message)))
      await outsider.close()
    })

    await t.test('captures the final real-browser UI after all interaction probes', async () => {
      await runtime.captureEvidence()
      const evidence = await runtime.audit()
      assert.ok(evidence.accepted.some(item => item.capability === 'peopleCounting.config.write'))
      assert.ok(evidence.rejected.some(item => item.reason === 'capability'))
      assert.ok(evidence.rejected.some(item => item.reason === 'payload'))
    })
  } finally {
    await runtime.close()
  }
})
