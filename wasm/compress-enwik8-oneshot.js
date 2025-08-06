import { compress } from './dist/index.mjs';
import { readFileSync, writeFileSync, statSync } from 'fs';
import { performance } from 'perf_hooks';

/**
 * One-shot compression of enwik8 file using Tamp
 */
async function compressEnwik8OneShot() {
  const inputFile = '../build/enwik8';
  const outputFile = '../build/enwik8-oneshot.tamp';

  console.log('🗜️  Tamp One-Shot Compression\n');

  try {
    // Check if input file exists
    const stats = statSync(inputFile);
    console.log(`📁 Input file: ${inputFile}`);
    console.log(`📊 Input size: ${stats.size.toLocaleString()} bytes (${(stats.size / 1024 / 1024).toFixed(2)} MB)\n`);

    // Read the entire file into memory
    console.log('📖 Reading input file...');
    const startRead = performance.now();
    const inputData = readFileSync(inputFile);
    const readTime = performance.now() - startRead;
    console.log(`✅ Read completed in ${readTime.toFixed(2)}ms\n`);

    // One-shot compression
    console.log('🗜️  Compressing with Tamp (one-shot)...');
    const startCompress = performance.now();
    const compressed = await compress(inputData);
    const compressTime = performance.now() - startCompress;
    console.log(`✅ Compression completed in ${compressTime.toFixed(2)}ms\n`);

    // Write compressed file
    console.log('💾 Writing compressed file...');
    const startWrite = performance.now();
    writeFileSync(outputFile, compressed);
    const writeTime = performance.now() - startWrite;
    console.log(`✅ Write completed in ${writeTime.toFixed(2)}ms\n`);

    // Calculate and display statistics
    const compressionRatio = compressed.length / inputData.length;
    const spaceSaved = inputData.length - compressed.length;
    const compressionSpeed = inputData.length / (compressTime / 1000) / 1024 / 1024; // MB/s
    const totalTime = readTime + compressTime + writeTime;

    console.log('📊 Compression Results:');
    console.log('========================');
    console.log(`Original size:     ${inputData.length.toLocaleString()} bytes`);
    console.log(`Compressed size:   ${compressed.length.toLocaleString()} bytes`);
    console.log(`Compression ratio: ${(compressionRatio * 100).toFixed(2)}%`);
    console.log(
      `Space saved:       ${spaceSaved.toLocaleString()} bytes (${((1 - compressionRatio) * 100).toFixed(2)}%)`
    );
    console.log('');
    console.log('⏱️  Performance:');
    console.log(`Read time:         ${readTime.toFixed(2)}ms`);
    console.log(`Compression time:  ${compressTime.toFixed(2)}ms`);
    console.log(`Write time:        ${writeTime.toFixed(2)}ms`);
    console.log(`Total time:        ${totalTime.toFixed(2)}ms`);
    console.log(`Compression speed: ${compressionSpeed.toFixed(2)} MB/s`);
    console.log('');
    console.log(`📁 Output file: ${outputFile}`);
    console.log('\n✅ One-shot compression completed successfully!');
  } catch (error) {
    console.error('\n❌ Compression failed:', error.message);
    if (error.stack) {
      console.error('Stack trace:', error.stack);
    }
    process.exit(1);
  }
}

// Run the compression
compressEnwik8OneShot();
