const path = require('path');
const HtmlWebpackPlugin = require('html-webpack-plugin');
const TerserPlugin = require('terser-webpack-plugin');
const CopyWebpackPlugin = require('copy-webpack-plugin');
const CompressionPlugin = require('compression-webpack-plugin');
const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const CssMinimizerPlugin = require('css-minimizer-webpack-plugin');

module.exports = (env, argv) => {
  const isProduction = argv.mode === 'production';

  return {
    mode: isProduction ? 'production' : 'development',
    entry: './js/main.js',
    output: {
      path: path.resolve(__dirname, '../build/pages-deploy'),
      filename: isProduction ? '[name].[contenthash].js' : '[name].js',
      clean: true,
    },

    devtool: isProduction ? false : 'eval-source-map',

    module: {
      rules: [
        {
          test: /\.css$/,
          use: [MiniCssExtractPlugin.loader, 'css-loader', 'postcss-loader'],
        },
        {
          test: /\.(png|jpg|jpeg|gif|svg|ico)$/,
          type: 'asset',
          parser: {
            dataUrlCondition: {
              maxSize: 4 * 1024,
            },
          },
        },
        {
          // Prevent code-splitting from Emscripten's import("module") (Node.js-only path).
          test: /tamp-module\.mjs$/,
          parser: { dynamicImportMode: 'eager' },
        },
      ],
    },

    plugins: [
      new HtmlWebpackPlugin({
        template: './index.html',
        filename: 'index.html',
        inject: 'head',
        scriptLoading: 'defer',
        minify: isProduction
          ? {
              removeComments: true,
              collapseWhitespace: true,
              removeRedundantAttributes: true,
              useShortDoctype: true,
              removeEmptyAttributes: true,
              removeStyleLinkTypeAttributes: true,
              keepClosingSlash: true,
              minifyJS: true,
              minifyCSS: true,
              minifyURLs: true,
            }
          : false,
      }),

      new CopyWebpackPlugin({
        patterns: [
          {
            from: '../assets/icon-transparent-bg-32x32.png',
            to: 'assets/[name][ext]',
            noErrorOnMissing: true,
          },
        ],
      }),

      new MiniCssExtractPlugin({
        filename: 'css/[name].[contenthash].css',
      }),

      new CompressionPlugin({
        algorithm: 'gzip',
        test: /\.(js|css|html|wasm)$/,
        threshold: 4096,
        minRatio: 0.8,
      }),

      new CompressionPlugin({
        algorithm: 'brotliCompress',
        test: /\.(js|css|html|wasm)$/,
        threshold: 4096,
        minRatio: 0.8,
        filename: '[path][base].br',
      }),
    ],

    optimization: {
      minimize: isProduction,
      minimizer: [
        new TerserPlugin({
          terserOptions: {
            compress: {
              drop_console: true,
              drop_debugger: true,
            },
          },
        }),
        new CssMinimizerPlugin(),
      ],
      usedExports: true,
      sideEffects: false,
      splitChunks: false,
    },

    devServer: {
      static: {
        directory: path.join(__dirname, '../build/pages-deploy'),
      },
      compress: true,
      port: 8000,
      open: true,
      hot: true,
    },

    resolve: {
      extensions: ['.js', '.mjs', '.json'],
      alias: {
        '@': path.resolve(__dirname, '.'),
      },
      fallback: {
        module: false,
        fs: false,
        path: false,
        crypto: false,
      },
    },

    experiments: {
      topLevelAwait: true,
    },
  };
};
