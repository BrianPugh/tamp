module.exports = {
  presets: [
    [
      '@babel/preset-env',
      {
        targets: 'last 2 versions, not dead, > 0.5%',
        modules: false, // Let webpack handle modules for better tree shaking
      },
    ],
  ],
};
