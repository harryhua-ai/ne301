import { createHash, createPublicKey, verify } from 'node:crypto'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import path from 'node:path'

const expectedBundleSha256 = '3cd01519af881eb114c2831d67b4f0542815c98140e06deeba14e597d947771c'
const expectedUiTbsSha256 = '59f695608f3caa2a06ffae486e6288b3e3052c277a87c5a8a0a3cfb3a049e7a6'
const expectedPublisherKeySha256 = '5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3'
const expectedFiles = new Map([
  ['assets/FileSaver.min-D428eXCv.js', ['37ca34e7ab99ebf9a005aaaee56a1004b196411f9410a0cd25c0ebf9024b7aec', 6939]],
  ['assets/index-CfTnTFvt.js', ['13b3071920115f597295e6f1002dae43b9d43797b3bafb4aedc7c619ead3737e', 269841]],
  ['assets/index-D-isHcyK.css', ['caa915f0cbaf7e9b499bebc8b7232cc85b96c359c8a3230040dee379794021f1', 41608]],
  ['assets/jmuxer.min.js', ['87b8dac054a9d2e06dacb8eccb1b3580c39af8634180bc36012c1b7d958c7442', 100839]],
  ['assets/videoWorker-CwSEKqUk.js', ['0f07f17eac982a2ff1eeff58b50c41a69d5237363397cd2ce5415a7c9ea0f439', 682]],
  ['assets/websocket-init-yiJ-u6et.js', ['9987a6188be31e521fa577338e11d85b2bfaa0ce4b67b263bdbcde3931df6800', 2000]],
  ['index.html', ['344dc49609dbd519411e098bf9d53ab9f9a08232ba6169e3f069f48d5f091532', 399]],
])

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex')
const readAscii = bytes => bytes.toString('ascii').replace(/\0+$/, '')

export function extractNeui(bundlePath, publicKeyPath, outputPath) {
  const bundle = readFileSync(bundlePath)
  const publicDer = readFileSync(publicKeyPath)
  const bundleSha256 = sha256(bundle)
  if (bundleSha256 !== expectedBundleSha256) throw new Error(`bundle sha256 mismatch: ${bundleSha256}`)
  if (bundle.subarray(0, 8).toString('hex') !== '4e45554901000000') throw new Error('NEUI v1 magic mismatch')
  const manifestLength = bundle.readUInt32LE(8)
  const payloadLength = bundle.readUInt32LE(12)
  const manifestStart = 16
  const payloadStart = manifestStart + manifestLength
  const signatureStart = payloadStart + payloadLength
  if (signatureStart >= bundle.length) throw new Error('truncated NEUI payload')
  const manifest = bundle.subarray(manifestStart, payloadStart)
  const payload = bundle.subarray(payloadStart, signatureStart)
  const signature = bundle.subarray(signatureStart)
  const publisherId = readAscii(manifest.subarray(0, 16))
  const appId = readAscii(manifest.subarray(16, 48))
  const publisherKeySha256 = manifest.subarray(48, 80).toString('hex')
  const nativeTbsSha256 = manifest.subarray(80, 112).toString('hex')
  const version = [manifest.readUInt16LE(112), manifest.readUInt16LE(114), manifest.readUInt16LE(116)].join('.')
  const fileCount = manifest.readUInt16LE(118)
  const hostUiAbi = manifest.readUInt32LE(120)
  const reserved = manifest.readUInt32LE(124)
  if (publisherId !== 'test-publisher' || appId !== 'line-crossing' || version !== '1.0.0' || hostUiAbi !== 1 || reserved !== 0) throw new Error('NEUI identity mismatch')
  if (publisherKeySha256 !== expectedPublisherKeySha256 || sha256(publicDer) !== expectedPublisherKeySha256) throw new Error('publisher public key mismatch')
  if (fileCount !== expectedFiles.size) throw new Error(`unexpected file count: ${fileCount}`)
  if (signature.length < 8 || signature.length > 72) throw new Error(`unexpected signature length: ${signature.length}`)
  if (sha256(bundle.subarray(0, signatureStart)) !== expectedUiTbsSha256) throw new Error('UI TBS sha256 mismatch')
  const publicKey = createPublicKey({ key: publicDer, format: 'der', type: 'spki' })
  if (!verify('sha256', bundle.subarray(0, signatureStart), publicKey, signature)) throw new Error('NEUI ECDSA signature verification failed')
  if (manifest.length < 128) throw new Error('short NEUI manifest')
  let manifestOffset = 128
  let payloadOffset = 0
  const files = []
  for (let index = 0; index < fileCount; index += 1) {
    if (manifestOffset + 38 > manifest.length) throw new Error(`truncated file record ${index}`)
    const pathLength = manifest.readUInt16LE(manifestOffset)
    const fileLength = manifest.readUInt32LE(manifestOffset + 2)
    const recordSha256 = manifest.subarray(manifestOffset + 6, manifestOffset + 38).toString('hex')
    const pathStart = manifestOffset + 38
    const filePath = manifest.subarray(pathStart, pathStart + pathLength).toString('ascii')
    if (!/^[A-Za-z0-9][A-Za-z0-9._-]*(?:\/[A-Za-z0-9][A-Za-z0-9._-]*)*$/.test(filePath)) throw new Error(`unsafe asset path: ${filePath}`)
    const record = expectedFiles.get(filePath)
    if (!record || record[0] !== recordSha256 || record[1] !== fileLength) throw new Error(`asset manifest mismatch: ${filePath}`)
    const contents = payload.subarray(payloadOffset, payloadOffset + fileLength)
    if (contents.length !== fileLength || sha256(contents) !== recordSha256) throw new Error(`asset content mismatch: ${filePath}`)
    const destination = path.join(outputPath, filePath)
    mkdirSync(path.dirname(destination), { recursive: true })
    writeFileSync(destination, contents)
    files.push({ path: filePath, bytes: fileLength, sha256: recordSha256 })
    manifestOffset = pathStart + pathLength
    payloadOffset += fileLength
  }
  if (manifestOffset !== manifest.length || payloadOffset !== payload.length) throw new Error('NEUI record lengths do not match manifest or payload')
  return {
    bundleBytes: bundle.length,
    bundleSha256,
    format: 'neui-sidecar-v1',
    publisherId,
    publisherKeySha256,
    appId,
    version,
    nativeTbsSha256,
    uiTbsSha256: expectedUiTbsSha256,
    hostUiAbi,
    signature: 'ECDSA-P256-SHA256:verified-with-copied-public-test-key',
    fileCount,
    files,
  }
}
