/**
 * Tests for extended format and lazy_matching options.
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import { compress, decompress, TampCompressor, TampDecompressor } from '../dist/index.mjs';

function assertArrayEqual(actual, expected, message = '') {
  if (actual.length !== expected.length) {
    throw new Error(`Array lengths differ: expected ${expected.length}, got ${actual.length}. ${message}`);
  }
  for (let i = 0; i < actual.length; i++) {
    if (actual[i] !== expected[i]) {
      throw new Error(`Arrays differ at index ${i}: expected ${expected[i]}, got ${actual[i]}. ${message}`);
    }
  }
}

describe('Extended Format Tests', () => {
  test('Round-trip with extended=true (default)', async () => {
    const data = new TextEncoder().encode('Hello, World! This is a test. '.repeat(10));
    const compressed = await compress(data);
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'extended=true round-trip should preserve data');
  });

  test('Round-trip with extended=false (v1 compatible)', async () => {
    const data = new TextEncoder().encode('Hello, World! This is a test. '.repeat(10));
    const compressed = await compress(data, { extended: false });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'extended=false round-trip should preserve data');
  });

  test('Extended produces smaller output for RLE data', async () => {
    // Long runs of the same byte are ideal for RLE encoding
    const rleData = new Uint8Array(500).fill(0x42);

    const compressedExtended = await compress(rleData, { extended: true });
    const compressedNonExtended = await compress(rleData, { extended: false });

    assert.ok(
      compressedExtended.length < compressedNonExtended.length,
      `Extended (${compressedExtended.length}) should be smaller than non-extended (${compressedNonExtended.length}) for RLE data`
    );

    // Verify both decompress correctly
    const decompressedExtended = await decompress(compressedExtended);
    const decompressedNonExtended = await decompress(compressedNonExtended);
    assertArrayEqual(decompressedExtended, rleData, 'Extended RLE decompression should match');
    assertArrayEqual(decompressedNonExtended, rleData, 'Non-extended RLE decompression should match');
  });

  test('Extended produces smaller output for long match data', async () => {
    // Repeating 16-byte pattern triggers extended match tokens
    const pattern = new TextEncoder().encode('Hello, World!!! '); // 16 bytes
    const data = new Uint8Array(pattern.length * 20);
    for (let i = 0; i < 20; i++) {
      data.set(pattern, i * pattern.length);
    }

    const compressedExtended = await compress(data, { extended: true });
    const compressedNonExtended = await compress(data, { extended: false });

    assert.ok(
      compressedExtended.length < compressedNonExtended.length,
      `Extended (${compressedExtended.length}) should be smaller than non-extended (${compressedNonExtended.length}) for pattern data`
    );

    const decompressedExtended = await decompress(compressedExtended);
    assertArrayEqual(decompressedExtended, data, 'Extended match decompression should match');
  });

  test('RLE with transitions between runs and non-RLE content', async () => {
    const parts = [
      new Uint8Array(50).fill(0x41), // 'A' x 50
      new TextEncoder().encode('The quick brown fox jumps!'),
      new Uint8Array(45).fill(0x42), // 'B' x 45
    ];
    const data = new Uint8Array(parts.reduce((sum, p) => sum + p.length, 0));
    let offset = 0;
    for (const part of parts) {
      data.set(part, offset);
      offset += part.length;
    }

    const compressed = await compress(data, { extended: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'RLE transition round-trip should preserve data');
  });

  test('RLE boundary values', async () => {
    for (const count of [2, 241, 500]) {
      const data = new Uint8Array(count).fill(0x5a); // 'Z'
      const compressed = await compress(data, { extended: true });
      const decompressed = await decompress(compressed);
      assertArrayEqual(decompressed, data, `RLE count=${count} round-trip should preserve data`);
    }
  });

  test('Extended with various window sizes', async () => {
    const data = new TextEncoder().encode('It was the best of times, it was the worst of times. '.repeat(10));

    for (const window of [8, 9, 10, 12]) {
      const compressed = await compress(data, { extended: true, window });
      const decompressed = await decompress(compressed);
      assertArrayEqual(decompressed, data, `Extended window=${window} round-trip should preserve data`);
    }
  });

  test('Extended with random data', async () => {
    const data = new Uint8Array(10000);
    // Deterministic pseudo-random fill
    let seed = 12345;
    for (let i = 0; i < data.length; i++) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      data[i] = (seed >> 16) & 0xff;
    }

    const compressed = await compress(data, { extended: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'Extended random data round-trip should preserve data');
  });

  test('Extended streaming compression', async () => {
    const data = new Uint8Array(200).fill(0x41); // RLE-heavy data
    const compressor = new TampCompressor({ extended: true });
    const decompressor = new TampDecompressor();

    try {
      const chunk1 = await compressor.compress(data.slice(0, 100));
      const chunk2 = await compressor.compress(data.slice(100));
      const flushed = await compressor.flush();

      const totalSize = chunk1.length + chunk2.length + flushed.length;
      const allCompressed = new Uint8Array(totalSize);
      let offset = 0;
      allCompressed.set(chunk1, offset);
      offset += chunk1.length;
      allCompressed.set(chunk2, offset);
      offset += chunk2.length;
      allCompressed.set(flushed, offset);

      const decompressed = await decompressor.decompress(allCompressed);
      assertArrayEqual(decompressed, data, 'Extended streaming round-trip should preserve data');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });

  test('Non-extended streaming compression', async () => {
    const data = new TextEncoder().encode('Streaming without extended. '.repeat(20));
    const compressor = new TampCompressor({ extended: false });
    const decompressor = new TampDecompressor();

    try {
      const compressed = await compressor.compress(data);
      const flushed = await compressor.flush();

      const allCompressed = new Uint8Array(compressed.length + flushed.length);
      allCompressed.set(compressed, 0);
      allCompressed.set(flushed, compressed.length);

      const decompressed = await decompressor.decompress(allCompressed);
      assertArrayEqual(decompressed, data, 'Non-extended streaming round-trip should preserve data');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });
});

describe('Lazy Matching Tests', () => {
  test('Round-trip with lazy_matching=true', async () => {
    const data = new TextEncoder().encode('The quick brown fox jumps over the lazy dog. '.repeat(20));
    const compressed = await compress(data, { lazy_matching: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'lazy_matching round-trip should preserve data');
  });

  test('Lazy matching with extended=true', async () => {
    const data = new TextEncoder().encode('The quick brown fox jumps over the lazy dog. '.repeat(20));
    const compressed = await compress(data, { extended: true, lazy_matching: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'extended+lazy round-trip should preserve data');
  });

  test('Lazy matching with extended=false', async () => {
    const data = new TextEncoder().encode('The quick brown fox jumps over the lazy dog. '.repeat(20));
    const compressed = await compress(data, { extended: false, lazy_matching: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'non-extended+lazy round-trip should preserve data');
  });

  test('Lazy matching with random data', async () => {
    const data = new Uint8Array(10000);
    let seed = 67890;
    for (let i = 0; i < data.length; i++) {
      seed = (seed * 1103515245 + 12345) & 0x7fffffff;
      data[i] = (seed >> 16) & 0xff;
    }

    const compressed = await compress(data, { lazy_matching: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'Lazy matching random data round-trip should preserve data');
  });

  test('Lazy matching with RLE-heavy data and extended', async () => {
    const parts = [
      new Uint8Array(100).fill(0x41),
      new TextEncoder().encode('break'),
      new Uint8Array(80).fill(0x42),
      new TextEncoder().encode('another break here'),
      new Uint8Array(60).fill(0x43),
    ];
    const data = new Uint8Array(parts.reduce((sum, p) => sum + p.length, 0));
    let offset = 0;
    for (const part of parts) {
      data.set(part, offset);
      offset += part.length;
    }

    const compressed = await compress(data, { extended: true, lazy_matching: true });
    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, data, 'Lazy+extended+RLE round-trip should preserve data');
  });
});
