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

    // Source maps for debugging
    devtool: isProduction ? 'source-map' : 'eval-source-map',

    module: {
      rules: [
        {
          test: /\.js$/,
          exclude: /node_modules/,
          use: {
            loader: 'babel-loader',
            options: {
              presets: ['@babel/preset-env'],
            },
          },
        },
        {
          test: /\.css$/,
          use: [
            MiniCssExtractPlugin.loader,
            'css-loader',
            'postcss-loader', // Add PostCSS processing with autoprefixer
          ],
        },
        {
          test: /\.(png|jpg|jpeg|gif|svg|ico)$/,
          type: 'asset',
          parser: {
            dataUrlCondition: {
              maxSize: 4 * 1024, // Inline assets under 4KB for faster loading
            },
          },
        },
        {
          test: /\.(woff|woff2|eot|ttf|otf)$/,
          type: 'asset/resource',
        },
        {
          test: /\.wasm$/,
          type: 'asset/resource',
        },
      ],
    },

    plugins: [
      new HtmlWebpackPlugin({
        template: './index.html',
        filename: 'index.html',
        inject: 'head', // Inject scripts in head for better loading
        scriptLoading: 'defer', // Use defer for better performance
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
            from: '../wasm/dist/*.{mjs,wasm}',
            to: 'wasm/dist/[name][ext]',
            noErrorOnMissing: true, // Don't fail if wasm dist doesn't exist
          },
          {
            from: '../assets/{icon-transparent-bg-32x32.png,logo-compressed.svg}',
            to: 'assets/[name][ext]',
            noErrorOnMissing: true,
          },
        ],
      }),

      new MiniCssExtractPlugin({
        filename: 'css/[name].[contenthash].css',
      }),

      new CompressionPlugin({
        test: /\.(js|css|html|wasm)$/,
        threshold: 4096, // Only compress files larger than 4KB
        minRatio: 0.8,
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
      // Enable tree shaking for dead code elimination
      usedExports: true,
      sideEffects: false,
      // Disable code splitting - single bundle for small website
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
