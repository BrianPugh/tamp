#!/usr/bin/env node

/**
 * Build script to create CommonJS and ES Module versions
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { minify } from 'terser';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const srcDir = path.join(__dirname, '..', 'src');
const distDir = path.join(__dirname, '..', 'dist');

// Check for minify flag
const shouldMinify = process.argv.includes('--minify');

// Ensure dist directory exists
if (!fs.existsSync(distDir)) {
  fs.mkdirSync(distDir, { recursive: true });
}

// Copy TypeScript definitions
const files = [
  { src: 'index.js', cjs: 'index.js', esm: 'index.mjs' },
  { src: 'tamp.js', cjs: 'tamp.js', esm: 'tamp.mjs' },
  { src: 'streams.js', cjs: 'streams.js', esm: 'streams.mjs' },
  { src: 'tamp.d.ts', out: 'index.d.ts' },
  { src: 'tamp.d.ts', out: 'tamp.d.ts' },
];

async function processFiles() {
  for (const file of files) {
    const srcPath = path.join(srcDir, file.src);

    if (!fs.existsSync(srcPath)) {
      console.warn(`Warning: Source file ${file.src} not found`);
      continue;
    }

    let content = fs.readFileSync(srcPath, 'utf8');

    // Create CommonJS version
    if (file.cjs) {
      let cjsContent = content;

      // Convert ES module imports/exports to CommonJS
      if (file.src.endsWith('.js')) {
        // Convert import statements
        cjsContent = cjsContent.replace(
          /import\s+(\{[^}]+\}|\*\s+as\s+\w+|\w+)\s+from\s+['"]([^'"]+)['"];?/g,
          (match, imports, module) => {
            if (module.startsWith('./')) {
              // Convert .mjs to .js for CommonJS
              module = module.replace(/\.mjs$/, '.js');
            }
            return `const ${imports} = require('${module}');`;
          }
        );

        // Collect all exports for final module.exports
        const namedExports = [];
        const exportedClasses = [];
        const exportedFunctions = [];
        const exportedConsts = [];

        // Find all exports
        const exportMatches = content.matchAll(/export\s+(?:async\s+)?(?:function|class|const)\s+(\w+)/g);
        for (const match of exportMatches) {
          namedExports.push(match[1]);
        }

        // Find export blocks
        const exportBlockMatches = content.matchAll(/export\s*\{([^}]+)\}/g);
        for (const match of exportBlockMatches) {
          const exports = match[1]
            .split(',')
            .map(e => e.trim())
            .filter(e => e.length > 0);
          namedExports.push(...exports);
        }

        // Remove export keywords but keep declarations
        cjsContent = cjsContent.replace(/export\s+(?=const|let|var|function|class|async)/g, '');

        // Remove export blocks entirely
        cjsContent = cjsContent.replace(/export\s*\{[^}]+\}[^;]*;?/g, '');

        // Add single module.exports at the end
        if (namedExports.length > 0) {
          const uniqueExports = [...new Set(namedExports)].filter(e => e.length > 0);
          if (uniqueExports.length > 0) {
            cjsContent += `\n\nmodule.exports = { ${uniqueExports.join(', ')} };\n`;
          }
        }
      }

      // Minify if requested and it's a JS file
      if (shouldMinify && file.src.endsWith('.js')) {
        try {
          const minified = await minify(cjsContent, {
            compress: true,
            mangle: true,
            format: {
              comments: false,
            },
          });
          cjsContent = minified.code || cjsContent;
        } catch (error) {
          console.warn(`Warning: Failed to minify ${file.cjs}:`, error.message);
        }
      }

      const cjsPath = path.join(distDir, file.cjs);
      fs.writeFileSync(cjsPath, cjsContent);
      console.log(`Created ${file.cjs}${shouldMinify && file.src.endsWith('.js') ? ' (minified)' : ''}`);
    }

    // Create ES Module version
    if (file.esm) {
      // Update import paths for ES modules
      let esmContent = content.replace(/from\s+['"]\.\/([^'"]+)\.js['"]/g, "from './$1.mjs'");

      // Minify if requested and it's a JS file
      if (shouldMinify && file.src.endsWith('.js')) {
        try {
          const minified = await minify(esmContent, {
            compress: true,
            mangle: true,
            format: {
              comments: false,
            },
          });
          esmContent = minified.code || esmContent;
        } catch (error) {
          console.warn(`Warning: Failed to minify ${file.esm}:`, error.message);
        }
      }

      const esmPath = path.join(distDir, file.esm);
      fs.writeFileSync(esmPath, esmContent);
      console.log(`Created ${file.esm}${shouldMinify && file.src.endsWith('.js') ? ' (minified)' : ''}`);
    }

    // Copy TypeScript definitions
    if (file.out) {
      const outPath = path.join(distDir, file.out);
      fs.writeFileSync(outPath, content);
      console.log(`Copied ${file.out}`);
    }
  }
}

await processFiles();

// Create streams TypeScript definitions
const streamsTypesDef = `/**
 * TypeScript type definitions for Tamp WebAssembly Streams
 */

export * from './index';

export declare class TampCompressionStream extends TransformStream<Uint8Array, Uint8Array> {
  constructor(options?: import('./index').TampOptions);
}

export declare class TampDecompressionStream extends TransformStream<Uint8Array, Uint8Array> {
  constructor(options?: import('./index').TampOptions);
}

export declare function compressStream(readable: ReadableStream<Uint8Array>, options?: import('./index').TampOptions): ReadableStream<Uint8Array>;
export declare function decompressStream(readable: ReadableStream<Uint8Array>, options?: import('./index').TampOptions): ReadableStream<Uint8Array>;
export declare function createReadableStream(data: Uint8Array, chunkSize?: number): ReadableStream<Uint8Array>;
export declare function collectStream(readable: ReadableStream<Uint8Array>): Promise<Uint8Array>;
`;

fs.writeFileSync(path.join(distDir, 'streams.d.ts'), streamsTypesDef);
console.log('Created streams.d.ts');

console.log('Build completed successfully!');
