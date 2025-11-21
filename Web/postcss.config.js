// postcss.config.js
export default {
  plugins: {
    autoprefixer: {
      overrideBrowserslist: [
        'Android >= 4.4',
        'iOS >= 9',
        'Chrome >= 60',
        'Safari >= 12',
        'Firefox >= 60',
        'Edge >= 79',
        '> 1%',
        'last 2 versions',
        'not dead'
      ],
      // Maintain shadcn/ui compatibility
      flexbox: 'no-2009',
      grid: 'autoplace',
      cascade: false,
      add: true,
      remove: false
    },
    // Keep plugins required by shadcn/ui
    'postcss-flexbugs-fixes': {},
    'postcss-preset-env': {
      stage: 3,
      features: {
        'custom-properties': true,
        'nesting-rules': true,
        'color-functional-notation': true,
        'logical-properties-and-values': false
      }
    },
    'postcss-custom-properties': {
      preserve: false
    },
    'postcss-nesting': {},
    'postcss-logical': {}
  }
}