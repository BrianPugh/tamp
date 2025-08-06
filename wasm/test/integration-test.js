/**
 * API Integration Test - validates that the JavaScript API works correctly
 * with the WebAssembly module (using mock implementation)
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import { TampCompressor, TampDecompressor, compress, decompress, TampError, initialize } from '../dist/index.mjs';

describe('API Integration Tests', () => {
  test('Module initialization', async () => {
    await initialize();
    // If we get here without throwing, initialization succeeded
    assert.ok(true, 'Module should initialize successfully');
  });

  test('Constructor creation', async () => {
    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    assert.ok(compressor instanceof TampCompressor, 'Should create TampCompressor instance');
    assert.ok(decompressor instanceof TampDecompressor, 'Should create TampDecompressor instance');

    compressor.destroy();
    decompressor.destroy();
  });

  test('Configuration options', async () => {
    const compressor = new TampCompressor({
      window: 8,
      literal: 7,
      dictionary: null,
    });

    assert.ok(compressor instanceof TampCompressor, 'Should create TampCompressor with config options');
    compressor.destroy();
  });

  test('Compression API call', async () => {
    const testData = new TextEncoder().encode('Hello, World!');
    const compressed = await compress(testData);

    assert.ok(compressed instanceof Uint8Array, 'Compressed data should be Uint8Array');
    assert.ok(compressed.length > 0, 'Compressed data should have length > 0');
  });

  test('Decompression API call', async () => {
    const testData = new TextEncoder().encode('Hello, World!');
    const compressed = await compress(testData);
    const decompressed = await decompress(compressed);

    assert.ok(decompressed instanceof Uint8Array, 'Decompressed data should be Uint8Array');
    assert.ok(decompressed.length > 0, 'Decompressed data should have length > 0');
  });

  test('Streaming compression', async () => {
    const compressor = new TampCompressor();
    const testData = new TextEncoder().encode('Streaming test');

    const chunk1 = await compressor.compress(testData);
    assert.ok(chunk1 instanceof Uint8Array, 'Streaming compression should return Uint8Array');

    compressor.destroy();
  });

  test('Compression flush', async () => {
    const compressor = new TampCompressor();
    const testData = new TextEncoder().encode('Streaming test');

    await compressor.compress(testData);
    const flushed = await compressor.flush();
    assert.ok(flushed instanceof Uint8Array, 'Compression flush should return Uint8Array');

    compressor.destroy();
  });

  test('Memory management', async () => {
    const compressor = new TampCompressor();
    compressor.destroy();

    // Should not throw on double destroy
    assert.doesNotThrow(() => {
      compressor.destroy();
    }, 'Double destroy should not throw');
  });

  test('TampError class', async () => {
    const error = new TampError(-1, 'Test error');

    assert.ok(error instanceof TampError, 'Should create TampError instance');
    assert.strictEqual(error.code, -1, 'Error code should be -1');
  });

  test('Uint8Array handling', async () => {
    const data = new Uint8Array([1, 2, 3, 4, 5]);
    const result = await compress(data);

    assert.ok(result instanceof Uint8Array, 'Should handle Uint8Array input and return Uint8Array');
  });
});
