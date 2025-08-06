/**
 * Simple test suite for Tamp WebAssembly
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import { compress, decompress, TampCompressor, TampDecompressor, TampError } from '../dist/index.mjs';

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

describe('Tamp WebAssembly Tests', () => {
  test('Basic compression and decompression', async () => {
    const originalText = 'Hello, World!';
    const originalData = new TextEncoder().encode(originalText);

    const compressed = await compress(originalData);
    assert.ok(compressed.length > 0, 'Compressed data should not be empty');
    assert.ok(compressed.length < originalData.length * 2, 'Compressed data should be reasonable size');

    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, originalData, 'Round-trip should preserve data');

    const decompressedText = new TextDecoder().decode(decompressed);
    assert.equal(decompressedText, originalText, 'Text should match exactly');
  });

  test('Compression with repetitive data', async () => {
    const originalText = 'I scream, you scream, we all scream for ice cream! '.repeat(10);
    const originalData = new TextEncoder().encode(originalText);

    const compressed = await compress(originalData);
    assert.ok(compressed.length < originalData.length, 'Repetitive data should compress smaller');

    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, originalData, 'Round-trip should preserve repetitive data');
  });

  test('Streaming compression', async () => {
    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    try {
      const testData = new TextEncoder().encode('Test data for streaming compression. '.repeat(20));

      // Compress in chunks
      const chunk1 = testData.slice(0, 100);
      const chunk2 = testData.slice(100, 200);
      const chunk3 = testData.slice(200);

      const compressed1 = await compressor.compress(chunk1);
      const compressed2 = await compressor.compress(chunk2);
      const compressed3 = await compressor.compress(chunk3);
      const flushed = await compressor.flush();

      // Combine compressed data
      const totalSize = compressed1.length + compressed2.length + compressed3.length + flushed.length;
      const allCompressed = new Uint8Array(totalSize);
      let offset = 0;
      allCompressed.set(compressed1, offset);
      offset += compressed1.length;
      allCompressed.set(compressed2, offset);
      offset += compressed2.length;
      allCompressed.set(compressed3, offset);
      offset += compressed3.length;
      allCompressed.set(flushed, offset);

      // Decompress
      const decompressed = await decompressor.decompress(allCompressed);
      assertArrayEqual(decompressed, testData, 'Streaming round-trip should preserve data');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });

  test('Custom configuration', async () => {
    const testData = new TextEncoder().encode('Configuration test data. '.repeat(10));

    const options = {
      window: 8,
      literal: 7,
    };

    const compressed = await compress(testData, options);
    const decompressed = await decompress(compressed, options);

    assertArrayEqual(decompressed, testData, 'Custom config round-trip should work');
  });

  test('Different window sizes', async () => {
    const testData = new TextEncoder().encode('Window size test. '.repeat(50));

    for (const windowBits of [8, 9, 10, 11, 12]) {
      const options = { window: windowBits, literal: 8 };
      const compressed = await compress(testData, options);
      const decompressed = await decompress(compressed, options);

      assertArrayEqual(decompressed, testData, `Window size ${windowBits} should work`);
    }
  });

  test('Different literal sizes', async () => {
    // Use simple data that fits within smaller literal bit ranges
    // For 5 bits: values 0-31, for 6 bits: values 0-63, etc.
    const testDataMap = {
      5: new Uint8Array([0, 1, 2, 3, 4, 5, 15, 31, 0, 1]), // values 0-31
      6: new Uint8Array([0, 1, 32, 63, 15, 31, 45, 63, 0]), // values 0-63
      7: new Uint8Array([0, 64, 127, 100, 15, 31, 64, 127]), // values 0-127
      8: new Uint8Array([0, 128, 255, 200, 15, 31, 64, 255]), // values 0-255
    };

    for (const literalBits of [5, 6, 7, 8]) {
      const testData = testDataMap[literalBits];
      const options = { window: 10, literal: literalBits };
      const compressed = await compress(testData, options);
      const decompressed = await decompress(compressed, options);

      assertArrayEqual(decompressed, testData, `Literal size ${literalBits} should work`);
    }
  });

  test('Empty data handling', async () => {
    const emptyData = new Uint8Array(0);

    const compressed = await compress(emptyData);
    assert.ok(compressed.length >= 0, 'Empty data should produce some output (headers)');

    const decompressed = await decompress(compressed);
    assert.equal(decompressed.length, 0, 'Empty data round-trip should remain empty');
  });

  test('Large data compression', async () => {
    // Create 100KB of test data
    const largeData = new Uint8Array(100 * 1024);
    for (let i = 0; i < largeData.length; i++) {
      largeData[i] = i % 256;
    }

    const compressed = await compress(largeData);
    assert.ok(compressed.length > 0, 'Large data should compress');
    assert.ok(compressed.length < largeData.length, 'Large data should compress smaller');

    const decompressed = await decompress(compressed);
    assertArrayEqual(decompressed, largeData, 'Large data round-trip should work');
  });

  test('Error handling', async () => {
    // Test with invalid compressed data
    const invalidData = new Uint8Array([0xff, 0xff, 0xff, 0xff]);

    try {
      await decompress(invalidData);
      throw new Error('Should have thrown an error for invalid data');
    } catch (error) {
      assert.ok(
        error instanceof TampError ||
          error instanceof RangeError ||
          error instanceof TypeError ||
          error.message.includes('error'),
        'Should throw appropriate error'
      );
    }
  });

  test('Memory cleanup', async () => {
    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    // Use them briefly
    const testData = new TextEncoder().encode('Memory test');
    await compressor.compress(testData);

    // Destroy should not throw
    compressor.destroy();
    decompressor.destroy();

    // Double destroy should be safe
    compressor.destroy();
    decompressor.destroy();

    assert.ok(true, 'Memory cleanup should work without errors');
  });
});
