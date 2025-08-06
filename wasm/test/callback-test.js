/**
 * Test file to verify callback functionality in tamp.js
 */

import { test, describe } from 'node:test';
import { strict as assert } from 'node:assert';
import { TampCompressor, compress } from '../dist/index.mjs';

describe('Tamp Callback Tests', () => {
  // Generate test data - need larger data to trigger multiple callbacks
  const testData = new Uint8Array(1024 * 1024); // 1MB of test data
  for (let i = 0; i < testData.length; i++) {
    testData[i] = i % 256;
  }

  test('TampCompressor class with progress callback', async () => {
    const compressor = new TampCompressor();
    let callbackCount = 0;
    let lastBytesProcessed = 0;

    const progressCallback = progressInfo => {
      callbackCount++;

      // Verify that progress is monotonically increasing
      assert.ok(
        progressInfo.bytesProcessed >= lastBytesProcessed,
        'Progress callback: bytes processed should not decrease'
      );
      lastBytesProcessed = progressInfo.bytesProcessed;

      // Return false to continue compression
      return false;
    };

    try {
      const compressed = await compressor.compress(testData, { onPoll: progressCallback });
      const flushed = await compressor.flush();

      assert.ok(compressed instanceof Uint8Array, 'Compressed data should be Uint8Array');
      assert.ok(flushed instanceof Uint8Array, 'Flushed data should be Uint8Array');
      assert.ok(callbackCount > 0, 'Callback should be called at least once');
    } finally {
      compressor.destroy();
    }
  });

  test('One-shot compress function with callback', async () => {
    let callbackCount = 0;
    const progressCallback = progressInfo => {
      callbackCount++;
      assert.ok(progressInfo.bytesProcessed >= 0, 'Bytes processed should be non-negative');
      assert.ok(progressInfo.totalBytes > 0, 'Total bytes should be positive');
      assert.ok(progressInfo.bytesProcessed <= progressInfo.totalBytes, 'Bytes processed should not exceed total');
      return false;
    };

    const compressed = await compress(testData, { onPoll: progressCallback });

    assert.ok(compressed instanceof Uint8Array, 'Compressed data should be Uint8Array');
    assert.ok(compressed.length > 0, 'Compressed data should not be empty');
    assert.ok(callbackCount > 0, 'One-shot callback should be called at least once');
  });

  test('Callback that aborts compression', async () => {
    const abortingCallback = progressInfo => {
      assert.ok(progressInfo.bytesProcessed >= 0, 'Bytes processed should be non-negative');
      assert.ok(progressInfo.totalBytes > 0, 'Total bytes should be positive');
      throw new Error('Aborting compression'); // Throw error to abort
    };

    await assert.rejects(
      async () => {
        await compress(testData, { onPoll: abortingCallback });
      },
      error => {
        return error.message.includes('Aborting compression');
      },
      'Compression should be aborted by callback'
    );
  });

  test('New progress callback API with rich information', async () => {
    let callbackCount = 0;
    let receivedProgressInfo = null;

    const progressCallback = progressInfo => {
      callbackCount++;
      receivedProgressInfo = progressInfo;

      // Verify progress info structure
      assert.ok(typeof progressInfo === 'object', 'Progress info should be an object');
      assert.ok(typeof progressInfo.bytesProcessed === 'number', 'bytesProcessed should be a number');
      assert.ok(typeof progressInfo.totalBytes === 'number', 'totalBytes should be a number');
      assert.ok(typeof progressInfo.percent === 'number', 'percent should be a number');
      assert.ok(typeof progressInfo.bytesPerSecond === 'number', 'bytesPerSecond should be a number');
      assert.ok(typeof progressInfo.estimatedTimeRemaining === 'number', 'estimatedTimeRemaining should be a number');
      assert.ok(typeof progressInfo.chunksProcessed === 'number', 'chunksProcessed should be a number');
      assert.ok(typeof progressInfo.elapsedTime === 'number', 'elapsedTime should be a number');

      // Verify progress values are reasonable
      assert.ok(progressInfo.bytesProcessed >= 0, 'bytesProcessed should be non-negative');
      assert.ok(progressInfo.totalBytes > 0, 'totalBytes should be positive');
      assert.ok(progressInfo.percent >= 0 && progressInfo.percent <= 100, 'percent should be between 0 and 100');
      assert.ok(progressInfo.bytesPerSecond >= 0, 'bytesPerSecond should be non-negative');
      assert.ok(progressInfo.estimatedTimeRemaining >= 0, 'estimatedTimeRemaining should be non-negative');
      assert.ok(progressInfo.chunksProcessed >= 0, 'chunksProcessed should be non-negative');
      assert.ok(progressInfo.elapsedTime >= 0, 'elapsedTime should be non-negative');

      // Don't abort
      return false;
    };

    const compressed = await compress(testData, { onPoll: progressCallback });

    assert.ok(compressed instanceof Uint8Array, 'Compressed data should be Uint8Array');
    assert.ok(compressed.length > 0, 'Compressed data should not be empty');
    assert.ok(callbackCount > 0, 'New progress callback should be called at least once');
    assert.ok(receivedProgressInfo !== null, 'Should have received progress info');
  });

  test('Callback throttling optimization', async () => {
    // Test data large enough to trigger multiple callbacks
    const largeTestData = new Uint8Array(2 * 1024 * 1024); // 2MB
    for (let i = 0; i < largeTestData.length; i++) {
      largeTestData[i] = i % 256;
    }

    // Test 1: No throttling (original behavior)
    let callbackCount1 = 0;
    await compress(largeTestData, {
      pollIntervalMs: 0, // No time throttling
      pollIntervalBytes: 0, // No byte throttling
      onPoll: () => {
        callbackCount1++;
        return false; // Continue
      },
    });

    // Test 2: With throttling (default behavior)
    let callbackCount2 = 0;
    await compress(largeTestData, {
      // Using defaults: pollIntervalMs: 100, pollIntervalBytes: 65536
      onPoll: () => {
        callbackCount2++;
        return false; // Continue
      },
    });

    // Test 3: Custom throttling settings
    let callbackCount3 = 0;
    await compress(largeTestData, {
      pollIntervalMs: 50, // 50ms throttling
      pollIntervalBytes: 32768, // 32KB throttling
      onPoll: () => {
        callbackCount3++;
        return false; // Continue
      },
    });

    // Verify throttling reduces callback frequency
    assert.ok(
      callbackCount2 < callbackCount1,
      `Default throttling should reduce callbacks: ${callbackCount2} < ${callbackCount1}`
    );

    // Custom throttling might have different count than defaults
    assert.ok(callbackCount3 > 0, 'Custom throttling should still call callbacks');

    console.log(`Callback throttling test results:`);
    console.log(`  No throttling: ${callbackCount1} callbacks`);
    console.log(`  Default throttling: ${callbackCount2} callbacks`);
    console.log(`  Custom throttling: ${callbackCount3} callbacks`);
    console.log(`  Reduction: ${(((callbackCount1 - callbackCount2) / callbackCount1) * 100).toFixed(1)}%`);
  });

  test('AbortController cancellation - pre-aborted signal', async () => {
    const controller = new AbortController();
    controller.abort('Test cancellation');

    await assert.rejects(
      async () => {
        await compress(testData, { signal: controller.signal });
      },
      error => {
        return (
          error.name === 'CompressionError' &&
          error.message === 'Compression was aborted' &&
          error.details.aborted === true &&
          error.details.reason === 'Test cancellation'
        );
      },
      'Should reject with CompressionError when signal is pre-aborted'
    );
  });

  test('AbortController cancellation - abort during compression', async () => {
    const controller = new AbortController();
    let callbackCount = 0;

    // Use large data to ensure compression takes some time
    const largeTestData = new Uint8Array(8 * 1024 * 1024); // 8MB
    for (let i = 0; i < largeTestData.length; i++) {
      largeTestData[i] = i % 256;
    }

    const compressionPromise = compress(largeTestData, {
      signal: controller.signal,
      pollIntervalMs: 1, // Very frequent callbacks to ensure we can abort
      pollIntervalBytes: 1024, // Small byte interval
      onPoll: () => {
        callbackCount++;
        // Abort after a few callbacks to ensure we're in the middle of compression
        if (callbackCount === 3) {
          controller.abort('Aborted during compression');
        }
        return false;
      },
    });

    await assert.rejects(
      async () => {
        await compressionPromise;
      },
      error => {
        return (
          error.name === 'CompressionError' &&
          error.message === 'Compression was aborted' &&
          error.details.aborted === true
        );
      },
      'Should reject with CompressionError when aborted during compression'
    );

    // Verify that callbacks were actually called before abortion
    assert.ok(callbackCount >= 3, `Should have made at least 3 callbacks, got ${callbackCount}`);
  });

  test('Async progress callback', async () => {
    let callbackCount = 0;
    const asyncProgressCallback = async progressInfo => {
      callbackCount++;
      // Simulate async operation
      await new Promise(resolve => setTimeout(resolve, 1));

      assert.ok(progressInfo.percent >= 0, 'Progress percent should be valid');
      return false; // Continue
    };

    const compressed = await compress(testData, { onPoll: asyncProgressCallback });

    assert.ok(compressed instanceof Uint8Array, 'Compressed data should be Uint8Array');
    assert.ok(callbackCount > 0, 'Async callback should be called at least once');
  });
});
