import terser from '@rollup/plugin-terser'

export default terser({
    compress: { 
      drop_console: ['log', 'warn', 'info', 'debug', 'trace', 'error'],
      drop_debugger: true,
    },
    mangle: true,
    output: { comments: false },
  })