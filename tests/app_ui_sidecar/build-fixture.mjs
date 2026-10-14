import assert from 'node:assert/strict'
import { createHash, createPrivateKey, createPublicKey, sign, verify } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { mkdirSync, readFileSync, readdirSync, realpathSync, rmSync, statSync, symlinkSync, utimesSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import { tmpdir } from 'node:os'
import { fileURLToPath } from 'node:url'
import preact from '@preact/preset-vite'
import tailwindcss from '@tailwindcss/vite'
import { build } from 'vite'

const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(fixtureRoot, '../..')
const sourceCommit = '6fb5dd798ef623316971816475be2208d9fbe018'
const tempRoot = path.join(realpathSync(tmpdir()), `ne301-app-ui-sidecar-${createHash('sha256').update(fixtureRoot).digest('hex').slice(0, 12)}`)
const sourceRoot = path.join(tempRoot, 'source')
const distRoot = path.join(tempRoot, 'dist')
const packageRoot = path.join(tempRoot, 'package')
const evidenceRoot = path.join(fixtureRoot, 'evidence')
const sourcePaths = [
  'Web/src/pages/peopleCounting/index.tsx',
  'Web/src/pages/peopleCounting/VideoPreview.tsx',
  'Web/src/pages/peopleCounting/StatsPanel.tsx',
  'Web/src/pages/peopleCounting/ConfigPanel.tsx',
  'Web/src/components/ui/card.tsx',
  'Web/src/components/ui/button.tsx',
  'Web/src/components/ui/label.tsx',
  'Web/src/components/ui/input.tsx',
  'Web/src/components/ui/switch.tsx',
  'Web/src/components/dialog.tsx',
  'Web/src/components/svg-icon.tsx',
  'Web/src/components/android-UI/simple-android-switch.tsx',
  'Web/src/components/android-UI/device-detector.ts',
  'Web/src/lib/utils.ts',
  'Web/src/services/api/peopleCounting.ts',
  'Web/src/services/api/deviceTool.ts',
  'Web/src/styles/index.css',
  'Web/src/styles/globals.css',
  'Web/src/locales/en/sys.json'
]
const remap = {
  'Web/src/services/request.ts': 'adapters/request.ts',
  'Web/src/utils/index.ts': 'adapters/utils.ts',
  'Web/src/lib/MSE/h264Player.ts': 'adapters/h264Player.ts',
  '@lingui/react': 'adapters/lingui.ts',
  sonner: 'adapters/sonner.ts'
}
const fixtureFiles = ['src/main.tsx', 'src/fixture.css', 'src/probe-worker.ts', 'adapters/bridge.ts']
const sha256 = value => createHash('sha256').update(value).digest('hex')
const writeSource = (repoPath, content) => {
  const destination = path.join(sourceRoot, repoPath.replace(/^Web\/src\//, ''))
  mkdirSync(path.dirname(destination), { recursive: true })
  writeFileSync(destination, content)
}
const gitShow = repoPath => execFileSync('git', ['show', `${sourceCommit}:${repoPath}`], { cwd: repoRoot, encoding: 'utf8' })
const collectFiles = directory => readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
  const full = path.join(directory, entry.name)
  return entry.isDirectory() ? collectFiles(full) : [full]
})
const canonicalize = value => JSON.stringify(value)

rmSync(tempRoot, { recursive: true, force: true })
mkdirSync(sourceRoot, { recursive: true })
mkdirSync(evidenceRoot, { recursive: true })
symlinkSync(path.join(fixtureRoot, 'node_modules'), path.join(sourceRoot, 'node_modules'), 'dir')
for (const sourcePath of sourcePaths) writeSource(sourcePath, gitShow(sourcePath))
for (const [repoPath, adapterPath] of Object.entries(remap)) {
  const targetPath = repoPath.replace(/^Web\/src\//, '')
  const adapter = readFileSync(path.join(fixtureRoot, adapterPath), 'utf8')
  writeSource(`Web/src/${targetPath}`, adapter)
}
const routeRequestPath = path.join(sourceRoot, 'services/request.ts')
mkdirSync(path.dirname(routeRequestPath), { recursive: true })
writeFileSync(routeRequestPath, readFileSync(path.join(fixtureRoot, 'adapters/request.ts'), 'utf8').replace("from './bridge'", "from '../adapters/bridge'"))
const closure = {
  sourceCommit,
  sourcePaths: sourcePaths.map(repoPath => ({ path: repoPath, sha256: sha256(gitShow(repoPath)) })),
  remappedDependencies: Object.entries(remap).map(([dependency, adapter]) => ({ dependency, adapter, sha256: sha256(readFileSync(path.join(fixtureRoot, adapter))) })),
  fixtureFiles: fixtureFiles.map(file => ({ path: file, sha256: sha256(readFileSync(path.join(fixtureRoot, file))) })),
  fixtureDependencies: JSON.parse(readFileSync(path.join(fixtureRoot, 'package.json'), 'utf8')).dependencies,
  fixtureDevDependencies: JSON.parse(readFileSync(path.join(fixtureRoot, 'package.json'), 'utf8')).devDependencies,
  entry: 'Web/src/pages/peopleCounting/index.tsx',
  excludedRootRequest: 'Web/src/request.ts',
  buildCommand: 'cd tests/app_ui_sidecar && npm ci && npm run build:fixture'
}
writeFileSync(path.join(evidenceRoot, 'source-closure.json'), `${JSON.stringify(closure, null, 2)}\n`)
writeFileSync(path.join(sourceRoot, 'index.html'), readFileSync(path.join(fixtureRoot, 'index.html')))
mkdirSync(path.join(sourceRoot, 'src'), { recursive: true })
mkdirSync(path.join(sourceRoot, 'adapters'), { recursive: true })
writeFileSync(path.join(sourceRoot, 'adapters/bridge.ts'), readFileSync(path.join(fixtureRoot, 'adapters/bridge.ts')))
writeFileSync(path.join(sourceRoot, 'src/fixture.css'), readFileSync(path.join(fixtureRoot, 'src/fixture.css')))
writeFileSync(path.join(sourceRoot, 'src/main.tsx'), readFileSync(path.join(fixtureRoot, 'src/main.tsx')))
writeFileSync(path.join(sourceRoot, 'src/probe-worker.ts'), readFileSync(path.join(fixtureRoot, 'src/probe-worker.ts')))

await build({
  configFile: false,
  root: sourceRoot,
  base: '/',
  plugins: [preact(), tailwindcss()],
  resolve: {
    alias: [
      { find: /^@\//, replacement: `${sourceRoot}/` },
      { find: /^@lingui\/react$/, replacement: path.join(fixtureRoot, 'adapters/lingui.ts') },
      { find: /^sonner$/, replacement: path.join(fixtureRoot, 'adapters/sonner.ts') },
      { find: /^react$/, replacement: 'preact/compat' },
      { find: /^react-dom$/, replacement: 'preact/compat' }
    ]
  },
  build: {
    outDir: distRoot,
    emptyOutDir: true,
    copyPublicDir: false,
    manifest: true,
    sourcemap: false,
    minify: true
  }
})

const files = collectFiles(distRoot).map(file => ({
  absolute: file,
  path: path.relative(distRoot, file).split(path.sep).join('/'),
  bytes: readFileSync(file)
})).sort((left, right) => left.path.localeCompare(right.path))
assert.ok(files.length > 0)
const payload = files.map(file => ({ path: file.path, size: file.bytes.length, sha256: sha256(file.bytes) }))
const bundleManifest = { format: 'sidecar-probe-bundle-v1', assets: payload }
const privateKey = createPrivateKey({
  key: Buffer.concat([Buffer.from('302e020100300506032b657004220420', 'hex'), Buffer.from(sha256(Buffer.from('ne301-app-ui-sidecar-feasibility-test-root-v1')), 'hex')]),
  format: 'der',
  type: 'pkcs8'
})
const publicKey = createPublicKey(privateKey)
const signedPayload = Buffer.from(canonicalize(bundleManifest))
const signedBytes = sign(null, signedPayload, privateKey)
assert.equal(verify(null, signedPayload, publicKey, signedBytes), true)
const manifestBytes = Buffer.from(JSON.stringify(bundleManifest))
mkdirSync(packageRoot, { recursive: true })
for (const file of files) {
  const destination = path.join(packageRoot, file.path)
  mkdirSync(path.dirname(destination), { recursive: true })
  writeFileSync(destination, file.bytes)
}
writeFileSync(path.join(packageRoot, 'manifest.json'), manifestBytes)
writeFileSync(path.join(packageRoot, 'manifest.sig'), signedBytes)
const normalizeTimes = directory => {
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const fullPath = path.join(directory, entry.name)
    if (entry.isDirectory()) normalizeTimes(fullPath)
    utimesSync(fullPath, new Date(0), new Date(0))
  }
}
normalizeTimes(packageRoot)
utimesSync(packageRoot, new Date(0), new Date(0))
const tarPath = path.join(tempRoot, 'fixture.tar')
execFileSync('tar', ['-cf', tarPath, '-C', packageRoot, '.'])
const archiveBytes = execFileSync('gzip', ['-n', '-9', '-c', tarPath]).length
const expandedBytes = collectFiles(packageRoot).reduce((sum, file) => sum + statSync(file).size, 0)
const metrics = {
  sourceCommit,
  outputFileCount: files.length + 2,
  assetFileCount: files.length,
  assetBytes: files.reduce((sum, file) => sum + file.bytes.length, 0),
  manifestBytes: manifestBytes.length,
  signatureBytes: signedBytes.length,
  expandedBundleBytes: expandedBytes,
  compressedArchiveBytes: archiveBytes,
  installStagePeakBytes: archiveBytes + expandedBytes,
  archiveMethod: 'tar archive with epoch timestamps plus gzip -n -9; not the NE301 package format',
  assets: payload,
}
assert.equal(metrics.assetBytes, files.reduce((sum, file) => sum + statSync(file.absolute).size, 0))
writeFileSync(path.join(evidenceRoot, 'fixture-metrics.json'), `${JSON.stringify(metrics, null, 2)}\n`)
process.stdout.write(`${JSON.stringify({ outputFileCount: metrics.outputFileCount, assetBytes: metrics.assetBytes, expandedBundleBytes: metrics.expandedBundleBytes, compressedArchiveBytes: metrics.compressedArchiveBytes, installStagePeakBytes: metrics.installStagePeakBytes }, null, 2)}\n`)
