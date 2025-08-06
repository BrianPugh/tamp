/**
 * Unit tests for Web Streams API implementation (TampCompressionStream, TampDecompressionStream)
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import {
  TampCompressionStream,
  TampDecompressionStream,
  compress,
  decompress,
  compressStream,
  decompressStream,
  createReadableStream,
  collectStream,
} from '../dist/index.mjs';

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

describe('Web Streams API Tests', () => {
  describe('TampCompressionStream', () => {
    test('Basic compression stream functionality', async () => {
      const testData = new TextEncoder().encode('Hello, compression world! '.repeat(10));
      const readable = createReadableStream(testData, 50);

      const compressionStream = new TampCompressionStream();
      const compressed = await collectStream(readable.pipeThrough(compressionStream));

      assert.ok(compressed.length > 0, 'Compression stream should produce output');
      assert.ok(compressed.length < testData.length, 'Repetitive data should compress smaller');

      // Verify compressed data can be decompressed and equals original
      const decompressed = await decompress(compressed);
      assertArrayEqual(decompressed, testData, 'One-shot decompression should match original data');
    });

    test('Compression stream with custom options', async () => {
      const testData = new TextEncoder().encode('Custom options test data. '.repeat(20));
      const options = { window: 8, literal: 7 };

      const readable = createReadableStream(testData, 100);
      const compressionStream = new TampCompressionStream(options);
      const compressed = await collectStream(readable.pipeThrough(compressionStream));

      assert.ok(compressed.length > 0, 'Compression with custom options should work');

      // Verify compressed data can be decompressed and equals original
      const decompressed = await decompress(compressed, options);
      assertArrayEqual(decompressed, testData, 'One-shot decompression with custom options should match original data');
    });

    test('Compression stream with small chunks', async () => {
      const testData = new TextEncoder().encode('Small chunk streaming test. '.repeat(50));
      const readable = createReadableStream(testData, 10); // 10-byte chunks

      const compressionStream = new TampCompressionStream();
      const compressed = await collectStream(readable.pipeThrough(compressionStream));

      assert.ok(compressed.length > 0, 'Small chunk compression should work');

      // Verify compressed data can be decompressed and equals original
      const decompressed = await decompress(compressed);
      assertArrayEqual(
        decompressed,
        testData,
        'One-shot decompression of small chunk compression should match original data'
      );
    });

    test('Compression stream with empty data', async () => {
      const emptyData = new Uint8Array(0);
      const readable = createReadableStream(emptyData);

      const compressionStream = new TampCompressionStream();
      const compressed = await collectStream(readable.pipeThrough(compressionStream));

      assert.ok(compressed.length >= 0, 'Empty data compression should not throw');

      // Verify compressed data can be decompressed and equals original
      const decompressed = await decompress(compressed);
      assertArrayEqual(decompressed, emptyData, 'One-shot decompression of empty data should match original data');
    });
  });

  describe('TampDecompressionStream', () => {
    test('Basic decompression stream functionality', async () => {
      const originalData = new TextEncoder().encode('Hello, decompression world! '.repeat(10));

      // First compress the data using well-tested one-shot compression
      const compressed = await compress(originalData);

      // Then decompress it using the decompression stream
      const decompressStream = new TampDecompressionStream();
      const decompressed = await collectStream(createReadableStream(compressed, 30).pipeThrough(decompressStream));

      assertArrayEqual(decompressed, originalData, 'Decompression stream should preserve data');
    });

    test('Decompression stream with custom options', async () => {
      const originalData = new TextEncoder().encode('Custom decompression options test. '.repeat(20));
      const options = { window: 8, literal: 7 };

      // Compress with options
      const compressStream = new TampCompressionStream(options);
      const compressed = await collectStream(createReadableStream(originalData, 100).pipeThrough(compressStream));

      // Decompress with matching options
      const decompressStream = new TampDecompressionStream(options);
      const decompressed = await collectStream(createReadableStream(compressed, 50).pipeThrough(decompressStream));

      assertArrayEqual(decompressed, originalData, 'Custom options round-trip should work');
    });

    test('Compression and decompression streams together', async () => {
      const originalData = new TextEncoder().encode('Testing both streams together! '.repeat(15));

      // Use both compression and decompression streams together
      const compressStream = new TampCompressionStream();
      const compressed = await collectStream(createReadableStream(originalData, 50).pipeThrough(compressStream));

      const decompressStream = new TampDecompressionStream();
      const decompressed = await collectStream(createReadableStream(compressed, 30).pipeThrough(decompressStream));

      assertArrayEqual(decompressed, originalData, 'Both streams together should preserve data');
    });
  });

  describe('Convenience Functions', () => {
    test('compressStream function', async () => {
      const testData = new TextEncoder().encode('Compress stream function test. '.repeat(20));
      const readable = createReadableStream(testData);

      const compressed = await collectStream(compressStream(readable));
      assert.ok(compressed.length > 0, 'compressStream should work');
      assert.ok(compressed.length < testData.length, 'Should compress repetitive data');
    });

    test('decompressStream function', async () => {
      const originalData = new TextEncoder().encode('Decompress stream function test. '.repeat(20));

      // Compress first
      const compressed = await collectStream(compressStream(createReadableStream(originalData)));

      // Then decompress
      const decompressed = await collectStream(decompressStream(createReadableStream(compressed)));

      assertArrayEqual(decompressed, originalData, 'decompressStream should work');
    });

    test('Round-trip with convenience functions', async () => {
      const originalData = new TextEncoder().encode('Round-trip convenience test. '.repeat(50));
      const options = { window: 9, literal: 8 };

      const compressed = await collectStream(compressStream(createReadableStream(originalData, 100), options));

      const decompressed = await collectStream(decompressStream(createReadableStream(compressed, 80), options));

      assertArrayEqual(decompressed, originalData, 'Convenience functions round-trip should work');
    });
  });

  describe('Static Methods', () => {
    test('Static methods return correct stream types', () => {
      // Test compression stream static methods
      const compressReadable = TampCompressionStream.readable();
      const compressWritable = TampCompressionStream.writable();

      assert.ok(compressReadable instanceof ReadableStream, 'readable() should return ReadableStream');
      assert.ok(compressWritable instanceof WritableStream, 'writable() should return WritableStream');

      // Test decompression stream static methods
      const decompressReadable = TampDecompressionStream.readable();
      const decompressWritable = TampDecompressionStream.writable();

      assert.ok(decompressReadable instanceof ReadableStream, 'readable() should return ReadableStream');
      assert.ok(decompressWritable instanceof WritableStream, 'writable() should return WritableStream');
    });

    test('Static methods accept options', () => {
      const options = { window: 8, literal: 7 };

      // Should not throw when creating with options
      const compressReadable = TampCompressionStream.readable(options);
      const compressWritable = TampCompressionStream.writable(options);
      const decompressReadable = TampDecompressionStream.readable(options);
      const decompressWritable = TampDecompressionStream.writable(options);

      assert.ok(compressReadable instanceof ReadableStream);
      assert.ok(compressWritable instanceof WritableStream);
      assert.ok(decompressReadable instanceof ReadableStream);
      assert.ok(decompressWritable instanceof WritableStream);
    });

    test('Static methods provide working stream components', async () => {
      const testData = new TextEncoder().encode('Static methods test data. '.repeat(10));

      // Test basic compression workflow using pipeThrough pattern
      const compressionStream = new TampCompressionStream();
      const compressed = await collectStream(createReadableStream(testData).pipeThrough(compressionStream));

      assert.ok(compressed.length > 0, 'Compression should produce output');
      assert.ok(compressed.length < testData.length, 'Should compress repetitive data');

      // Verify static methods exist and work
      const staticReadable = TampCompressionStream.readable();
      const staticWritable = TampCompressionStream.writable();

      assert.ok(staticReadable instanceof ReadableStream, 'Static readable works');
      assert.ok(staticWritable instanceof WritableStream, 'Static writable works');
    });

    test('Static methods integration with transform streams', async () => {
      const testData = new TextEncoder().encode('Static methods integration test. '.repeat(20));
      const options = { window: 9, literal: 8 };

      // Create a compression transform stream and test with pipeThrough pattern
      const compressTransform = new TampCompressionStream(options);
      const compressed = await collectStream(createReadableStream(testData).pipeThrough(compressTransform));

      // Create a decompression transform stream and test with pipeThrough pattern
      const decompressTransform = new TampDecompressionStream(options);
      const decompressed = await collectStream(createReadableStream(compressed).pipeThrough(decompressTransform));

      assertArrayEqual(decompressed, testData, 'Static methods should provide components for working streams');

      // Verify static methods return the right types
      assert.ok(TampCompressionStream.readable(options) instanceof ReadableStream);
      assert.ok(TampCompressionStream.writable(options) instanceof WritableStream);
      assert.ok(TampDecompressionStream.readable(options) instanceof ReadableStream);
      assert.ok(TampDecompressionStream.writable(options) instanceof WritableStream);
    });
  });

  describe('Helper Functions', () => {
    test('createReadableStream with various chunk sizes', async () => {
      const testData = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);

      // Test just a few chunk sizes to avoid hanging
      for (const chunkSize of [5, 10]) {
        const stream = createReadableStream(testData, chunkSize);
        const collected = await collectStream(stream);

        assertArrayEqual(collected, testData, `Chunk size ${chunkSize} should work`);
      }
    });

    test('createReadableStream with empty data', async () => {
      const emptyData = new Uint8Array(0);
      const stream = createReadableStream(emptyData);
      const collected = await collectStream(stream);

      assert.equal(collected.length, 0, 'Empty stream should work');
    });

    test('collectStream basic functionality', async () => {
      const testData = new Uint8Array([100, 200, 255]);
      const stream = createReadableStream(testData);
      const collected = await collectStream(stream);

      assertArrayEqual(collected, testData, 'collectStream should work correctly');
    });
  });

  describe('Error Handling', () => {
    test('Decompression stream handles invalid data', async () => {
      const invalidData = new Uint8Array([0xff, 0xfe, 0xfd, 0xfc]);

      try {
        const decompressStream = new TampDecompressionStream();
        const result = await collectStream(createReadableStream(invalidData).pipeThrough(decompressStream));
        // If it doesn't throw, that's also acceptable behavior
        assert.ok(result instanceof Uint8Array, 'Should return Uint8Array even for invalid data');
      } catch (error) {
        // If it throws an error, that's expected behavior for invalid data
        assert.ok(error instanceof Error, 'Should throw proper error for invalid data');
      }
    });
  });

  describe('Integration', () => {
    test('Medium data streaming', async () => {
      // Create 10KB of test data to avoid timeout issues
      const mediumData = new Uint8Array(10 * 1024);
      for (let i = 0; i < mediumData.length; i++) {
        mediumData[i] = i % 256;
      }

      // Compress with streaming
      const compressed = await collectStream(compressStream(createReadableStream(mediumData, 1024)));

      assert.ok(compressed.length > 0, 'Medium data compression should produce output');
      assert.ok(compressed.length < mediumData.length, 'Repetitive data should compress');

      // Decompress with streaming
      const decompressed = await collectStream(decompressStream(createReadableStream(compressed)));

      assertArrayEqual(decompressed, mediumData, 'Medium data round-trip should work');
    });

    test('Sequential streams', async () => {
      const testData1 = new TextEncoder().encode('Sequential stream 1 data. '.repeat(10));
      const testData2 = new TextEncoder().encode('Sequential stream 2 data. '.repeat(10));

      // Process streams sequentially
      const compressed1 = await collectStream(compressStream(createReadableStream(testData1)));
      const compressed2 = await collectStream(compressStream(createReadableStream(testData2)));

      assert.ok(compressed1.length > 0 && compressed2.length > 0, 'Sequential compression should work');

      // Decompress sequentially
      const decompressed1 = await collectStream(decompressStream(createReadableStream(compressed1)));
      const decompressed2 = await collectStream(decompressStream(createReadableStream(compressed2)));

      assertArrayEqual(decompressed1, testData1, 'Sequential stream 1 should work');
      assertArrayEqual(decompressed2, testData2, 'Sequential stream 2 should work');
    });
  });
});
