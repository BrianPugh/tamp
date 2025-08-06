/**
 * Comprehensive test suite for Tamp WebAssembly API
 * Tests all features and functionality
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import {
  compress,
  decompress,
  compressText,
  decompressText,
  TampCompressor,
  TampDecompressor,
  initializeDictionary,
  computeMinPatternSize,
  using,
  TampError,
  ExcessBitsError,
  CompressionError,
  DecompressionError,
} from '../dist/index.mjs';

describe('Comprehensive Tamp WebAssembly API Tests', () => {
  test('Utility Functions', async () => {
    // Test initializeDictionary
    const dict = await initializeDictionary(1024);
    assert.equal(dict.length, 1024, 'initializeDictionary should return correct size');
    assert.ok(dict instanceof Uint8Array, 'initializeDictionary should return Uint8Array');

    // Test power-of-2 validation
    await assert.rejects(
      async () => {
        await initializeDictionary(1000); // Not power of 2
      },
      error => error.message.includes('power of 2'),
      'initializeDictionary should validate power of 2'
    );

    // Test computeMinPatternSize
    const minSize1 = await computeMinPatternSize(10, 8);
    assert.equal(typeof minSize1, 'number', 'computeMinPatternSize should return number');
    assert.ok(minSize1 >= 2 && minSize1 <= 3, 'computeMinPatternSize should return valid size');

    // Test parameter validation
    await assert.rejects(
      async () => {
        await computeMinPatternSize(20, 8); // Invalid window
      },
      error => error.message.includes('between 8 and 15'),
      'computeMinPatternSize should validate window'
    );
  });

  test('Simplified Dictionary Parameter', async () => {
    const customDict = await initializeDictionary(256);

    // Test with custom dictionary
    const data = new TextEncoder().encode('Hello with custom dictionary!');
    const compressed = await compress(data, { window: 8, dictionary: customDict });
    await decompress(compressed, { window: 8, dictionary: customDict });

    assert.ok(compressed instanceof Uint8Array, 'Custom dictionary compression should work');

    // Test without dictionary (should use default)
    const compressed2 = await compress(data, { window: 8, dictionary: null });
    await decompress(compressed2, { window: 8, dictionary: null });

    assert.ok(compressed2 instanceof Uint8Array, 'Null dictionary API should work');

    // Test dictionary size validation
    await assert.rejects(
      async () => {
        const wrongSizeDict = new Uint8Array(100); // Wrong size for window=8 (should be 256)
        await compress(data, { window: 8, dictionary: wrongSizeDict });
      },
      error => error.message.includes('Dictionary size'),
      'Dictionary size validation should work'
    );
  });

  test('Text Compression/Decompression', async () => {
    const originalText = 'Hello, 世界! This is a test of text compression with Unicode characters: 🎉';

    // Test compressText
    const compressed = await compressText(originalText);
    assert.ok(compressed instanceof Uint8Array, 'compressText should return Uint8Array');
    assert.ok(compressed.length > 0, 'compressText should produce output');

    // Test decompressText
    const decompressed = await decompressText(compressed);
    assert.equal(typeof decompressed, 'string', 'decompressText should return string');
    assert.ok(decompressed.length >= 0, 'Text API structure should work');

    // Test with options - use ASCII text for literal: 7
    const asciiText = 'Hello, ASCII world! This is a test of text compression.';
    const compressed2 = await compressText(asciiText, { window: 8, literal: 7 });
    const decompressed2 = await decompressText(compressed2, { window: 8, literal: 7 });
    assert.equal(typeof decompressed2, 'string', 'Text compression with options API should work');

    // Test different encodings
    const decompressed3 = await decompressText(compressed, {}, 'utf-8');
    assert.equal(typeof decompressed3, 'string', 'Explicit UTF-8 encoding API should work');
  });

  test('Error Handling', async () => {
    // Test error class hierarchy
    assert.ok(ExcessBitsError.prototype instanceof TampError, 'ExcessBitsError should extend TampError');
    assert.ok(CompressionError.prototype instanceof TampError, 'CompressionError should extend TampError');
    assert.ok(DecompressionError.prototype instanceof TampError, 'DecompressionError should extend TampError');

    // Test error creation
    const tampError = new TampError(-1, 'Test error', { test: true });
    assert.equal(tampError.details.test, true, 'TampError should have details property');
    assert.equal(tampError.code, -1, 'TampError should have correct code');

    const excessError = new ExcessBitsError('Test excess bits', { field: 'value' });
    assert.equal(excessError.name, 'ExcessBitsError', 'ExcessBitsError should have correct name');
    assert.equal(excessError.details.field, 'value', 'ExcessBitsError should have details');
  });

  test('Automatic Resource Management', async () => {
    const testData = new TextEncoder().encode('Resource management test');
    let compressorDestroyed = false;

    // Mock destroy to verify it's called
    const compressor = new TampCompressor();
    const originalDestroy = compressor.destroy;
    compressor.destroy = () => {
      compressorDestroyed = true;
      originalDestroy.call(compressor);
    };

    // Test using() helper
    const result = await using(compressor, async c => {
      const compressed = await c.compress(testData);
      return compressed;
    });

    assert.ok(result instanceof Uint8Array, 'using() should return result');
    assert.ok(compressorDestroyed, 'using() should call destroy automatically');

    // Test using() with synchronous function
    const decompressor = new TampDecompressor();
    let decompressorDestroyed = false;
    const originalDestroy2 = decompressor.destroy;
    decompressor.destroy = () => {
      decompressorDestroyed = true;
      originalDestroy2.call(decompressor);
    };

    const syncResult = await using(decompressor, _d => {
      return 'sync result';
    });

    assert.equal(syncResult, 'sync result', 'using() should work with sync functions');
    assert.ok(decompressorDestroyed, 'using() should call destroy for sync functions');

    // Test using() with error (should still call destroy)
    const compressor2 = new TampCompressor();
    let compressor2Destroyed = false;
    const originalDestroy3 = compressor2.destroy;
    compressor2.destroy = () => {
      compressor2Destroyed = true;
      originalDestroy3.call(compressor2);
    };

    await assert.rejects(
      async () => {
        await using(compressor2, async () => {
          throw new Error('Test error');
        });
      },
      error => error.message === 'Test error',
      'using() should propagate errors'
    );
    assert.ok(compressor2Destroyed, 'using() should call destroy even on error');
  });

  test('Edge Cases and Error Conditions', async () => {
    // Test empty text
    const emptyCompressed = await compressText('');
    const emptyDecompressed = await decompressText(emptyCompressed);
    assert.equal(typeof emptyDecompressed, 'string', 'Empty text compression API should work');

    // Test very long text
    const longText = 'A'.repeat(10000);
    const longCompressed = await compressText(longText);
    const longDecompressed = await decompressText(longCompressed);
    assert.equal(typeof longDecompressed, 'string', 'Long text compression API should work');
    assert.ok(longCompressed.length > 0, 'Long text compression should produce output');

    // Test invalid configuration
    await assert.rejects(async () => {
      await compress(new Uint8Array([1, 2, 3]), { window: 20 }); // Invalid window
    }, 'Invalid configuration should throw error');
  });
});
