module.exports = {
  plugins: [
    require('postcss-preset-env')({
      browsers: 'defaults, not ie 11',
      autoprefixer: { grid: false },
    }),
  ],
};
