# Tamp WebAssembly

High-performance JavaScript/TypeScript compression library using Tamp compiled
to WebAssembly with modern, ergonomic callback support.

See [the docs] for usage. This README is tamp-developer-centric

## Installation

```bash
npm install @brianpugh/tamp
```

## Quick Start

```javascript
import {
  compress,
  decompress,
  compressText,
  decompressText,
} from '@brianpugh/tamp';

// Text compression
const compressed = await compressText('Hello, 世界! 🎉');
const original = await decompressText(compressed);

// Binary data
const data = new TextEncoder().encode('Hello, World!');
const compressedData = await compress(data);
const decompressed = await decompress(compressedData);
```

## Building from Source

Requirements:

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
- Node.js 14+

```bash
# Install Emscripten
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Build
cd tamp/wasm
make all
npm run build
```

## Testing

Run the test suite using Node.js built-in test runner:

```bash
npm test
```
