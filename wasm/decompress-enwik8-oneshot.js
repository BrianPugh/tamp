import { decompress } from './dist/index.mjs';
import { readFileSync, writeFileSync, statSync } from 'fs';
import { performance } from 'perf_hooks';

/**
 * One-shot decompression and verification of enwik8 file using Tamp
 */
async function decompressEnwik8OneShot() {
  const originalFile = '../datasets/enwik8';
  const compressedFile = '../build/enwik8-oneshot.tamp';
  const reconstructedFile = '../build/enwik8-oneshot-reconstructed';

  console.log('🔄 Tamp One-Shot Decompression & Verification\n');

  try {
    // Check if files exist
    const originalStats = statSync(originalFile);
    const compressedStats = statSync(compressedFile);

    console.log(`📁 Original file: ${originalFile}`);
    console.log(
      `📊 Original size: ${originalStats.size.toLocaleString()} bytes (${(originalStats.size / 1024 / 1024).toFixed(
        2
      )} MB)`
    );
    console.log(`📁 Compressed file: ${compressedFile}`);
    console.log(
      `📊 Compressed size: ${compressedStats.size.toLocaleString()} bytes (${(
        compressedStats.size /
        1024 /
        1024
      ).toFixed(2)} MB)\n`
    );

    // Read the original file
    console.log('📖 Reading original file...');
    const startReadOriginal = performance.now();
    const originalData = readFileSync(originalFile);
    const readOriginalTime = performance.now() - startReadOriginal;
    console.log(`✅ Original read completed in ${readOriginalTime.toFixed(2)}ms`);

    // Read the compressed file
    console.log('📖 Reading compressed file...');
    const startReadCompressed = performance.now();
    const compressedData = readFileSync(compressedFile);
    const readCompressedTime = performance.now() - startReadCompressed;
    console.log(`✅ Compressed read completed in ${readCompressedTime.toFixed(2)}ms\n`);

    // One-shot decompression
    console.log('🔄 Decompressing with Tamp (one-shot)...');
    const startDecompress = performance.now();
    const decompressed = await decompress(compressedData);
    const decompressTime = performance.now() - startDecompress;
    console.log(`✅ Decompression completed in ${decompressTime.toFixed(2)}ms\n`);

    // Verify that decompressed data matches original
    console.log('🔍 Verifying decompressed data...');
    const startVerify = performance.now();
    const matches = Buffer.compare(originalData, decompressed) === 0;
    const verifyTime = performance.now() - startVerify;

    // Save reconstructed file for external comparison
    console.log(`💾 Saving reconstructed file to ${reconstructedFile}...`);
    const startSave = performance.now();
    writeFileSync(reconstructedFile, decompressed);
    const saveTime = performance.now() - startSave;
    console.log(`✅ Reconstructed file saved in ${saveTime.toFixed(2)}ms`);

    if (matches) {
      console.log(`✅ Verification passed in ${verifyTime.toFixed(2)}ms: Decompressed data matches original!`);
    } else {
      console.error(`❌ Verification failed in ${verifyTime.toFixed(2)}ms: Decompressed data does not match original!`);
      console.error(`Original length: ${originalData.length}, Decompressed length: ${decompressed.length}`);
      process.exit(1);
    }

    // Calculate and display statistics
    const compressionRatio = compressedData.length / originalData.length;
    const decompressionSpeed = decompressed.length / (decompressTime / 1000) / 1024 / 1024; // MB/s
    const totalTime = readOriginalTime + readCompressedTime + decompressTime + verifyTime + saveTime;

    console.log('\n📊 Decompression Results:');
    console.log('==========================');
    console.log(`Original size:      ${originalData.length.toLocaleString()} bytes`);
    console.log(`Compressed size:    ${compressedData.length.toLocaleString()} bytes`);
    console.log(`Decompressed size:  ${decompressed.length.toLocaleString()} bytes`);
    console.log(`Compression ratio:  ${(compressionRatio * 100).toFixed(2)}%`);
    console.log('');
    console.log('⏱️  Performance:');
    console.log(`Original read time:    ${readOriginalTime.toFixed(2)}ms`);
    console.log(`Compressed read time:  ${readCompressedTime.toFixed(2)}ms`);
    console.log(`Decompression time:    ${decompressTime.toFixed(2)}ms`);
    console.log(`Verification time:     ${verifyTime.toFixed(2)}ms`);
    console.log(`Save time:             ${saveTime.toFixed(2)}ms`);
    console.log(`Total time:            ${totalTime.toFixed(2)}ms`);
    console.log(`Decompression speed:   ${decompressionSpeed.toFixed(2)} MB/s`);

    console.log('\n✅ One-shot decompression and verification completed successfully!');
  } catch (error) {
    console.error('\n❌ Decompression failed:', error.message);
    if (error.stack) {
      console.error('Stack trace:', error.stack);
    }
    process.exit(1);
  }
}

// Run the decompression
decompressEnwik8OneShot();
