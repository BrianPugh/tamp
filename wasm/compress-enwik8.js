import { compress, TampCompressor, using } from './dist/index.mjs';
import { readFileSync, writeFileSync, statSync } from 'fs';
import { performance } from 'perf_hooks';

/**
 * Compress enwik8 file using Tamp compression
 */
async function compressEnwik8() {
  const inputFile = '../datasets/enwik8';
  const outputFile = '../build/enwik8-js.tamp';

  console.log('🗜️  Tamp Compression Benchmark\n');

  try {
    // Check if input file exists
    const stats = statSync(inputFile);
    console.log(`📁 Input file: ${inputFile}`);
    console.log(`📊 Input size: ${stats.size.toLocaleString()} bytes (${(stats.size / 1024 / 1024).toFixed(2)} MB)`);

    // Read the input file
    console.log('\n📖 Reading input file...');
    const startRead = performance.now();
    const inputData = readFileSync(inputFile);
    const readTime = performance.now() - startRead;
    console.log(`✅ Read completed in ${readTime.toFixed(2)}ms`);

    // Compress using one-shot API
    console.log('\n🗜️  Compressing with Tamp...');
    const startCompress = performance.now();

    // Use streaming compression for better memory efficiency with large files
    const compressed = await using(new TampCompressor(), async compressor => {
      const chunks = [];
      const chunkSize = 1024 * 1024; // 1MB chunks

      console.log(`   Processing ${Math.ceil(inputData.length / chunkSize)} chunks...`);

      for (let offset = 0; offset < inputData.length; offset += chunkSize) {
        const chunk = inputData.slice(offset, offset + chunkSize);
        const compressedChunk = await compressor.compress(chunk);
        if (compressedChunk.length > 0) {
          chunks.push(compressedChunk);
        }

        if ((offset / chunkSize + 1) % 10 === 0) {
          console.log(`   Processed ${offset / chunkSize + 1} chunks...`);
        }
      }

      // Flush remaining data
      const flushed = await compressor.flush();
      if (flushed.length > 0) {
        chunks.push(flushed);
      }

      // Combine all chunks
      const totalSize = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
      const result = new Uint8Array(totalSize);
      let resultOffset = 0;
      for (const chunk of chunks) {
        result.set(chunk, resultOffset);
        resultOffset += chunk.length;
      }

      return result;
    });

    const compressTime = performance.now() - startCompress;

    // Write compressed file
    console.log('\n💾 Writing compressed file...');
    const startWrite = performance.now();
    writeFileSync(outputFile, compressed);
    const writeTime = performance.now() - startWrite;

    // Calculate statistics
    const compressionRatio = compressed.length / inputData.length;
    const spaceSaved = inputData.length - compressed.length;
    const compressionSpeed = inputData.length / (compressTime / 1000) / 1024 / 1024; // MB/s

    console.log('\n📊 Compression Results:');
    console.log(`   Original size:     ${inputData.length.toLocaleString()} bytes`);
    console.log(`   Compressed size:   ${compressed.length.toLocaleString()} bytes`);
    console.log(`   Compression ratio: ${(compressionRatio * 100).toFixed(2)}%`);
    console.log(
      `   Space saved:       ${spaceSaved.toLocaleString()} bytes (${((1 - compressionRatio) * 100).toFixed(2)}%)`
    );
    console.log(`   Compression time:  ${compressTime.toFixed(2)}ms`);
    console.log(`   Compression speed: ${compressionSpeed.toFixed(2)} MB/s`);
    console.log(`   Write time:        ${writeTime.toFixed(2)}ms`);
    console.log(`   Total time:        ${(readTime + compressTime + writeTime).toFixed(2)}ms`);
    console.log(`   Output file:       ${outputFile}`);

    console.log('\n✅ Compression completed successfully!');
  } catch (error) {
    console.error('\n❌ Compression failed:', error.message);
    console.error('Error details:', error);
    process.exit(1);
  }
}

// Alternative: Simple one-shot compression (less memory efficient for large files)
async function compressEnwik8Simple() {
  const inputFile = '../datasets/enwik8';
  const outputFile = '../build/enwik8.tamp.simple';

  console.log('🗜️  Tamp Simple Compression\n');

  try {
    console.log('📖 Reading input file...');
    const inputData = readFileSync(inputFile);
    console.log(`📊 Input size: ${inputData.length.toLocaleString()} bytes`);

    console.log('\n🗜️  Compressing...');
    const startTime = performance.now();
    const compressed = await compress(inputData);
    const compressTime = performance.now() - startTime;

    console.log('💾 Writing output...');
    writeFileSync(outputFile, compressed);

    const compressionRatio = compressed.length / inputData.length;
    console.log('\n📊 Results:');
    console.log(`   Compression ratio: ${(compressionRatio * 100).toFixed(2)}%`);
    console.log(`   Time: ${compressTime.toFixed(2)}ms`);
    console.log(`   Output: ${outputFile}`);

    console.log('\n✅ Simple compression completed!');
  } catch (error) {
    console.error('\n❌ Simple compression failed:', error.message);
    process.exit(1);
  }
}

// Check command line arguments
const useSimple = process.argv.includes('--simple');

if (useSimple) {
  compressEnwik8Simple();
} else {
  compressEnwik8();
}
