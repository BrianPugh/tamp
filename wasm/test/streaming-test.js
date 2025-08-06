import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import { TampCompressor, TampDecompressor, using, TampError, DecompressionError } from '../dist/index.mjs';

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

describe('Comprehensive Streaming Tests', () => {
  test('Multi-chunk streaming compression and decompression', async () => {
    const testData = new TextEncoder().encode('Multi-chunk streaming test data. '.repeat(500));

    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    try {
      // Compress data in multiple chunks
      const compressed1 = await compressor.compress(testData.slice(0, 1000));
      const compressed2 = await compressor.compress(testData.slice(1000, 2000));
      const compressed3 = await compressor.compress(testData.slice(2000));
      const flushed = await compressor.flush();

      // Combine all compressed data
      const totalCompressedSize = compressed1.length + compressed2.length + compressed3.length + flushed.length;
      const allCompressed = new Uint8Array(totalCompressedSize);
      let offset = 0;
      allCompressed.set(compressed1, offset);
      offset += compressed1.length;
      allCompressed.set(compressed2, offset);
      offset += compressed2.length;
      allCompressed.set(compressed3, offset);
      offset += compressed3.length;
      allCompressed.set(flushed, offset);

      // Decompress the combined data
      const decompressed = await decompressor.decompress(allCompressed);
      assertArrayEqual(decompressed, testData, 'Multi-chunk streaming should preserve data');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });

  test('Streaming with various chunk sizes', async () => {
    const testData = new TextEncoder().encode('Variable chunk size test. '.repeat(100));
    const chunkSizes = [1, 10, 100, 1000];

    for (const chunkSize of chunkSizes) {
      const compressor = new TampCompressor();
      const decompressor = new TampDecompressor();

      try {
        const compressedChunks = [];

        // Compress in chunks of the specified size
        for (let i = 0; i < testData.length; i += chunkSize) {
          const chunk = testData.slice(i, i + chunkSize);
          const compressed = await compressor.compress(chunk);
          if (compressed.length > 0) {
            compressedChunks.push(compressed);
          }
        }

        const flushed = await compressor.flush();
        if (flushed.length > 0) {
          compressedChunks.push(flushed);
        }

        // Combine all compressed chunks
        const totalSize = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
        const allCompressed = new Uint8Array(totalSize);
        let offset = 0;
        for (const chunk of compressedChunks) {
          allCompressed.set(chunk, offset);
          offset += chunk.length;
        }

        // Decompress
        const decompressed = await decompressor.decompress(allCompressed);
        assertArrayEqual(decompressed, testData, `Chunk size ${chunkSize} should work`);
      } finally {
        compressor.destroy();
        decompressor.destroy();
      }
    }
  });

  test('Streaming error handling - Invalid compressed data', async () => {
    const decompressor = new TampDecompressor();

    try {
      // Try to decompress invalid data
      const invalidData = new Uint8Array([0xff, 0xfe, 0xfd, 0xfc]);

      await assert.rejects(
        async () => {
          await decompressor.decompress(invalidData);
        },
        error => {
          return (
            error instanceof TampError ||
            error instanceof DecompressionError ||
            error.message.includes('error') ||
            error.message.includes('invalid') ||
            error.message.includes('Header reading')
          );
        },
        'Should throw error for invalid compressed data'
      );
    } finally {
      decompressor.destroy();
    }
  });

  test('Streaming error handling - Resource cleanup on error', async () => {
    let compressorDestroyed = false;
    let decompressorDestroyed = false;

    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    // Mock destroy methods to track cleanup
    const originalCompressorDestroy = compressor.destroy;
    const originalDecompressorDestroy = decompressor.destroy;

    compressor.destroy = function () {
      compressorDestroyed = true;
      originalCompressorDestroy.call(this);
    };

    decompressor.destroy = function () {
      decompressorDestroyed = true;
      originalDecompressorDestroy.call(this);
    };

    try {
      // Test using() helper with compressor error
      await assert.rejects(
        async () => {
          await using(compressor, async () => {
            throw new Error('Simulated error');
          });
        },
        error => error.message === 'Simulated error',
        'using() should propagate error'
      );

      assert.ok(compressorDestroyed, 'Compressor should be destroyed even on error');

      // Test decompressor cleanup on error
      try {
        await using(decompressor, async d => {
          const invalidData = new Uint8Array([0x00, 0x01, 0x02]);
          await d.decompress(invalidData);
        });
      } catch (error) {
        // Expected to fail
      }

      assert.ok(decompressorDestroyed, 'Decompressor should be destroyed even on error');
    } catch (error) {
      // Make sure resources are cleaned up even if test fails
      if (!compressorDestroyed) compressor.destroy();
      if (!decompressorDestroyed) decompressor.destroy();
      throw error;
    }
  });

  test('Empty data streaming', async () => {
    const emptyData = new Uint8Array(0);

    const compressor = new TampCompressor();
    try {
      const compressed = await compressor.compress(emptyData);
      const flushed = await compressor.flush();

      // Should produce some output (headers)
      assert.ok(compressed.length >= 0, 'Empty data compression should not throw');
      assert.ok(flushed.length >= 0, 'Empty data flush should not throw');

      // Test decompression
      const totalSize = compressed.length + flushed.length;
      const combinedCompressed = new Uint8Array(totalSize);
      combinedCompressed.set(compressed);
      combinedCompressed.set(flushed, compressed.length);

      const decompressor = new TampDecompressor();
      try {
        const decompressed = await decompressor.decompress(combinedCompressed);
        assert.equal(decompressed.length, 0, 'Empty data round-trip should remain empty');
      } finally {
        decompressor.destroy();
      }
    } finally {
      compressor.destroy();
    }
  });

  test('Large data streaming compression', async () => {
    // Create 1MB of test data
    const largeData = new Uint8Array(1024 * 1024);
    for (let i = 0; i < largeData.length; i++) {
      largeData[i] = i % 256;
    }

    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    try {
      // Compress in 64KB chunks
      const chunkSize = 64 * 1024;
      const compressedChunks = [];

      for (let i = 0; i < largeData.length; i += chunkSize) {
        const chunk = largeData.slice(i, i + chunkSize);
        const compressed = await compressor.compress(chunk);
        if (compressed.length > 0) {
          compressedChunks.push(compressed);
        }
      }

      const flushed = await compressor.flush();
      if (flushed.length > 0) {
        compressedChunks.push(flushed);
      }

      // Combine compressed chunks
      const totalSize = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
      const allCompressed = new Uint8Array(totalSize);
      let offset = 0;
      for (const chunk of compressedChunks) {
        allCompressed.set(chunk, offset);
        offset += chunk.length;
      }

      assert.ok(allCompressed.length > 0, 'Large data compression should produce output');
      assert.ok(allCompressed.length < largeData.length, 'Large repetitive data should compress smaller');

      // Decompress and verify
      const decompressed = await decompressor.decompress(allCompressed);
      assertArrayEqual(decompressed, largeData, 'Large data streaming should preserve data');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });

  test('Streaming with custom compression options', async () => {
    const testData = new TextEncoder().encode('Custom options streaming test data. '.repeat(50));

    const options = {
      window: 8,
      literal: 7,
    };

    const compressor = new TampCompressor(options);
    const decompressor = new TampDecompressor(options);

    try {
      // Compress in chunks
      const compressed1 = await compressor.compress(testData.slice(0, 100));
      const compressed2 = await compressor.compress(testData.slice(100));
      const flushed = await compressor.flush();

      // Combine compressed data
      const totalSize = compressed1.length + compressed2.length + flushed.length;
      const allCompressed = new Uint8Array(totalSize);
      let offset = 0;
      allCompressed.set(compressed1, offset);
      offset += compressed1.length;
      allCompressed.set(compressed2, offset);
      offset += compressed2.length;
      allCompressed.set(flushed, offset);

      // Decompress with matching options
      const decompressed = await decompressor.decompress(allCompressed);
      assertArrayEqual(decompressed, testData, 'Custom options streaming should work');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });

  test('Streaming memory management', async () => {
    const testData = new TextEncoder().encode('Memory management test. '.repeat(100));

    // Test multiple compressor/decompressor instances
    for (let i = 0; i < 5; i++) {
      const compressor = new TampCompressor();
      const decompressor = new TampDecompressor();

      try {
        const compressed = await compressor.compress(testData);
        const flushed = await compressor.flush();

        const totalSize = compressed.length + flushed.length;
        const allCompressed = new Uint8Array(totalSize);
        allCompressed.set(compressed);
        allCompressed.set(flushed, compressed.length);

        const decompressed = await decompressor.decompress(allCompressed);
        assertArrayEqual(decompressed, testData, `Memory management iteration ${i} should work`);
      } finally {
        compressor.destroy();
        decompressor.destroy();
      }
    }

    assert.ok(true, 'Multiple streaming instances should work without memory issues');
  });

  test('Streaming with incremental data feeding', async () => {
    const baseText = 'Incremental streaming test. ';
    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    try {
      const compressedChunks = [];

      // Feed data incrementally, one character at a time initially
      const singleChar = new TextEncoder().encode('A');
      let compressed = await compressor.compress(singleChar);
      if (compressed.length > 0) compressedChunks.push(compressed);

      // Then feed larger chunks
      for (let i = 0; i < 10; i++) {
        const chunk = new TextEncoder().encode(`${baseText}${i} `);
        compressed = await compressor.compress(chunk);
        if (compressed.length > 0) compressedChunks.push(compressed);
      }

      const flushed = await compressor.flush();
      if (flushed.length > 0) compressedChunks.push(flushed);

      // Combine all compressed data
      const totalSize = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
      const allCompressed = new Uint8Array(totalSize);
      let offset = 0;
      for (const chunk of compressedChunks) {
        allCompressed.set(chunk, offset);
        offset += chunk.length;
      }

      // Decompress and verify we get reasonable output
      const decompressed = await decompressor.decompress(allCompressed);
      assert.ok(decompressed.length > 0, 'Incremental streaming should produce output');

      // Verify it contains our test data
      const decompressedText = new TextDecoder().decode(decompressed);
      assert.ok(decompressedText.includes('A'), 'Should contain single character');
      assert.ok(decompressedText.includes(baseText), 'Should contain base text');
    } finally {
      compressor.destroy();
      decompressor.destroy();
    }
  });
});
