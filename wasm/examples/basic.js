/**
 * Basic Tamp WebAssembly usage examples
 */

import { compress, decompress, TampCompressor, TampDecompressor } from '../src/index.js';

async function basicExample() {
  console.log('=== Basic Compression/Decompression Example ===');

  // Sample data to compress
  const originalText = 'I scream, you scream, we all scream for ice cream! '.repeat(10);
  const originalData = new TextEncoder().encode(originalText);

  console.log(`Original size: ${originalData.length} bytes`);
  console.log(`Original text: "${originalText.substring(0, 50)}..."`);

  try {
    // One-shot compression
    const compressed = await compress(originalData);
    console.log(`Compressed size: ${compressed.length} bytes`);
    console.log(`Compression ratio: ${(originalData.length / compressed.length).toFixed(2)}:1`);

    // One-shot decompression
    const decompressed = await decompress(compressed);
    const decompressedText = new TextDecoder().decode(decompressed);

    console.log(`Decompressed size: ${decompressed.length} bytes`);
    console.log(`Round-trip successful: ${originalText === decompressedText}`);
  } catch (error) {
    console.error('Error in basic example:', error);
  }
}

async function streamingExample() {
  console.log('\n=== Streaming Compression Example ===');

  const text = 'This is a test of streaming compression. '.repeat(100);
  const data = new TextEncoder().encode(text);

  try {
    // Create compressor instance
    const compressor = new TampCompressor();
    const decompressor = new TampDecompressor();

    console.log(`Original size: ${data.length} bytes`);

    // Compress in chunks
    const chunkSize = 64;
    const compressedChunks = [];

    for (let i = 0; i < data.length; i += chunkSize) {
      const chunk = data.slice(i, i + chunkSize);
      const compressed = await compressor.compress(chunk);
      if (compressed.length > 0) {
        compressedChunks.push(compressed);
      }
    }

    // Get final flush data
    const finalData = await compressor.flush();
    if (finalData.length > 0) {
      compressedChunks.push(finalData);
    }

    // Combine all compressed chunks
    const totalCompressedSize = compressedChunks.reduce((sum, chunk) => sum + chunk.length, 0);
    const allCompressed = new Uint8Array(totalCompressedSize);
    let offset = 0;
    for (const chunk of compressedChunks) {
      allCompressed.set(chunk, offset);
      offset += chunk.length;
    }

    console.log(`Compressed size: ${allCompressed.length} bytes`);
    console.log(`Compression ratio: ${(data.length / allCompressed.length).toFixed(2)}:1`);

    // Decompress
    const decompressed = await decompressor.decompress(allCompressed);
    const decompressedText = new TextDecoder().decode(decompressed);

    console.log(`Decompressed size: ${decompressed.length} bytes`);
    console.log(`Streaming round-trip successful: ${text === decompressedText}`);

    // Clean up
    compressor.destroy();
    decompressor.destroy();
  } catch (error) {
    console.error('Error in streaming example:', error);
  }
}

async function customConfigExample() {
  console.log('\n=== Custom Configuration Example ===');

  const data = new TextEncoder().encode('Hello, World! '.repeat(50));

  const configs = [
    { window: 8, literal: 8, name: 'Small window' },
    { window: 10, literal: 8, name: 'Default' },
    { window: 12, literal: 8, name: 'Large window' },
    { window: 10, literal: 7, name: '7-bit literals' },
  ];

  console.log(`Original size: ${data.length} bytes`);

  for (const config of configs) {
    try {
      const compressed = await compress(data, config);
      const decompressed = await decompress(compressed, config);

      const success = data.every((byte, i) => byte === decompressed[i]);

      console.log(
        `${config.name} (window=${config.window}, literal=${config.literal}): ` +
          `${compressed.length} bytes, ` +
          `ratio ${(data.length / compressed.length).toFixed(2)}:1, ` +
          `valid: ${success}`
      );
    } catch (error) {
      console.error(`Error with ${config.name}:`, error);
    }
  }
}

// Run all examples
async function runExamples() {
  try {
    await basicExample();
    await streamingExample();
    await customConfigExample();
    console.log('\n=== All examples completed successfully! ===');
  } catch (error) {
    console.error('Error running examples:', error);
  }
}

// Check if running directly (not imported)
if (import.meta.url === `file://${process.argv[1]}`) {
  runExamples();
}

export { basicExample, streamingExample, customConfigExample };
