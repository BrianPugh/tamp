import { defineConfig } from 'tsup';
import { copyFileSync } from 'fs';

const shared = {
  entry: ['src/index.js', 'src/tamp.js', 'src/streams.js'],
  outDir: 'dist',
  splitting: false,
  clean: false,
};

export default defineConfig([
  {
    ...shared,
    format: ['esm'],
    external: ['./tamp-module.mjs'],
    outExtension: () => ({ js: '.mjs' }),
    onSuccess: async () => {
      // Copy TypeScript declarations
      for (const file of ['index', 'tamp', 'streams']) {
        copyFileSync(`src/${file}.d.ts`, `dist/${file}.d.ts`);
      }
    },
  },
  {
    ...shared,
    format: ['cjs'],
    outExtension: () => ({ js: '.cjs' }),
    esbuildPlugins: [
      {
        name: 'rewrite-mjs-to-js',
        setup(build) {
          build.onResolve({ filter: /\.\/tamp-module\.mjs$/ }, () => ({
            path: './tamp-module.js',
            external: true,
          }));
        },
      },
    ],
  },
]);
