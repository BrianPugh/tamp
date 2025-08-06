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

describe('Simple Compression Tests', () => {
  test('Basic compression and decompression round-trip', async () => {
    const originalText = 'Hello, World! This is a test of compression. '.repeat(3);
    const originalData = new TextEncoder().encode(originalText);

    const compressed = await compress(originalData);
    assert.ok(compressed.length > 0, 'Compressed data should not be empty');

    const decompressed = await decompress(compressed);
    assert.equal(decompressed.length, originalData.length, 'Decompressed length should match original');

    assertArrayEqual(decompressed, originalData, 'Decompressed data should match original');

    const decompressedText = new TextDecoder().decode(decompressed);
    assert.equal(decompressedText, originalText, 'Decompressed text should match original');
  });
});
