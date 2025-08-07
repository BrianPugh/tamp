# @brianpugh/tamp

---

**Documentation:** <https://tamp.readthedocs.io/en/latest/>

**Source Code:** <https://github.com/BrianPugh/tamp>

**Online Demo:** <https://brianpugh.github.io/tamp>

---

Tamp is a low-memory, DEFLATE-inspired lossless compression library optimized
for embedded and resource-constrained environments.

Tamp delivers the highest data compression ratios, while using the least amount
of RAM and firmware storage.

## Features

- High compression ratios, low memory use, and fast.
- Various language implementations available:
  - Python
  - Micropython
  - C
  - Javascript
- Support for both streaming and one-shot compression

## Installation

```bash
npm install @brianpugh/tamp
```

## Quick Start

### Text Compression

```javascript
import { compressText, decompressText } from '@brianpugh/tamp';

// Compress text (handles UTF-8 encoding automatically)
const compressed = await compressText(
  'Hello, World! This is some text to compress.'
);
console.log(`Original: ${compressed.length} bytes compressed`);

// Decompress back to original text
const original = await decompressText(compressed);
console.log(original); // "Hello, World! This is some text to compress."
```

### Binary Data Compression

```javascript
import { compress, decompress } from '@brianpugh/tamp';

// Compress binary data
const data = new TextEncoder().encode('Hello, World!');
const compressedData = await compress(data);

// Decompress
const decompressed = await decompress(compressedData);
console.log(new TextDecoder().decode(decompressed)); // "Hello, World!"
```

### Streaming Compression

```javascript
import { TampCompressor, TampDecompressor } from '@brianpugh/tamp';

// Create streaming compressor
const compressor = new TampCompressor({ window: 10 });

// Compress data in chunks
const chunk1 = await compressor.compress(new TextEncoder().encode('Hello '));
const chunk2 = await compressor.compress(new TextEncoder().encode('World!'));
const finalChunk = await compressor.flush();

// Clean up
compressor.destroy();

// Create streaming decompressor
const decompressor = new TampDecompressor();

// Decompress the chunks we created above
const decompressed1 = await decompressor.decompress(chunk1);
const decompressed2 = await decompressor.decompress(chunk2);
const decompressed3 = await decompressor.decompress(finalChunk);

// Combine all decompressed chunks
const totalLength =
  decompressed1.length + decompressed2.length + decompressed3.length;
const result = new Uint8Array(totalLength);
result.set(decompressed1, 0);
result.set(decompressed2, decompressed1.length);
result.set(decompressed3, decompressed1.length + decompressed2.length);

// Convert back to text
const originalText = new TextDecoder().decode(result);
console.log(originalText); // "Hello World!"

// Clean up
decompressor.destroy();
```

## API Reference

See [the docs](https://tamp.readthedocs.io/en/latest/javascript.html) for more
details.

### Basic Interface

- `compress(data: Uint8Array): Promise<Uint8Array>` - Compress binary data
- `decompress(data: Uint8Array): Promise<Uint8Array>` - Decompress binary data
- `compressText(text: string): Promise<Uint8Array>` - Compress UTF-8 text
- `decompressText(data: Uint8Array): Promise<string>` - Decompress to UTF-8 text

### Streaming Classes

#### TampCompressor

```typescript
class TampCompressor {
  constructor(options?: { window?: number; literal?: number });
  compress(data: Uint8Array): Promise<Uint8Array>;
  flush(): Promise<Uint8Array>;
  destroy(): void;
}
```

#### TampDecompressor

```typescript
class TampDecompressor {
  decompress(data: Uint8Array): Promise<Uint8Array>;
  destroy(): void;
}
```

## License

This project is licensed under the Apache 2.0 License - see the
[LICENSE](https://github.com/BrianPugh/tamp/blob/main/LICENSE) file for details.
