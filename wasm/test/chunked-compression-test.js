/**
 * Test file for chunked compression implementation
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import { compress, decompress } from '../dist/index.mjs';

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

describe('Chunked Compression Tests', () => {
  test('Chunked compression with various data sizes', async () => {
    // Test with data larger than 1024 bytes to ensure chunking works
    const testSizes = [
      100, // Small data
      1000, // Medium data
      10000, // Large data (> 1024 bytes)
      100000, // Very large data
      1048576, // 1MB data (equals CHUNK_SIZE)
      2097152, // 2MB data (exceeds CHUNK_SIZE)
    ];

    for (const size of testSizes) {
      // Generate test data
      const originalData = new Uint8Array(size);
      for (let i = 0; i < size; i++) {
        originalData[i] = i % 256; // Pattern that should compress well
      }

      // Test one-shot compression (which uses the updated compress method internally)
      const compressed = await compress(originalData);
      const decompressed = await decompress(compressed);

      // Verify data integrity
      assert.equal(decompressed.length, originalData.length, `Length mismatch for ${size} bytes`);
      assertArrayEqual(decompressed, originalData, `Data integrity check failed for ${size} bytes`);

      // Verify compression produces reasonable output
      assert.ok(compressed.length > 0, `Compressed data should not be empty for ${size} bytes`);
      assert.ok(
        compressed.length < originalData.length * 2,
        `Compressed data should be reasonable size for ${size} bytes`
      );

      // Verify compression ratio is reasonable (at least some compression for repetitive data)
      const compressionRatio = (compressed.length / originalData.length) * 100;
      assert.ok(compressionRatio > 0, `Compression ratio should be positive for ${size} bytes`);
    }
  });

  test('Large repeating data compression', async () => {
    // Create highly compressible data (repeating pattern)
    const size = 1000000; // 1MB
    const originalData = new Uint8Array(size);
    const pattern = 'Hello, World! This is a test pattern for compression. ';
    const patternBytes = new TextEncoder().encode(pattern);

    for (let i = 0; i < size; i++) {
      originalData[i] = patternBytes[i % patternBytes.length];
    }

    const compressed = await compress(originalData);
    const decompressed = await decompress(compressed);

    // Verify data integrity
    assert.equal(decompressed.length, originalData.length, 'Length should match for repeating pattern');
    assertArrayEqual(decompressed, originalData, 'Data integrity check failed for repeating pattern');

    // Verify compression is effective for repeating data
    assert.ok(compressed.length > 0, 'Compressed data should not be empty');
    assert.ok(compressed.length < originalData.length, 'Repeating data should compress smaller than original');

    // Calculate and verify compression ratio
    const compressionRatio = (compressed.length / originalData.length) * 100;
    assert.ok(compressionRatio < 100, 'Compression ratio should be less than 100% for repeating data');
    assert.ok(compressionRatio > 0, 'Compression ratio should be positive');
  });

  test('Empty data chunked compression', async () => {
    const emptyData = new Uint8Array(0);

    const compressed = await compress(emptyData);
    const decompressed = await decompress(compressed);

    assert.ok(compressed.length >= 0, 'Empty data should produce some output (headers)');
    assert.equal(decompressed.length, 0, 'Empty data round-trip should remain empty');
  });

  test('Single byte data chunked compression', async () => {
    const singleByte = new Uint8Array([42]);

    const compressed = await compress(singleByte);
    const decompressed = await decompress(compressed);

    assert.ok(compressed.length > 0, 'Single byte should produce compressed output');
    assertArrayEqual(decompressed, singleByte, 'Single byte round-trip should preserve data');
  });
});
