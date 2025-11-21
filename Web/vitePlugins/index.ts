// import compression from './compression'
// import visualizer from './visualizer
import mock from './mock'
import svgIcons from './svgIcons'
import cleanCode from './cleanCode'
import version from './version'
import type { PluginOption } from 'vite'

const plugins: PluginOption[] = [
  mock,
  // compression,
  // visualizer,
  cleanCode,
  svgIcons,
  version(), // Call function to return plugin object
]

export default plugins