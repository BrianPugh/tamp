/**
 * Tamp JavaScript Bindings Benchmark
 *
 * Measures compression and decompression performance on real datasets.
 * Use this to compare performance before/after optimizations.
 *
 * Usage:
 *   node benchmark.js                      # Run with default settings (enwik8)
 *   node benchmark.js --iterations 5       # Run 5 iterations
 *   node benchmark.js --input data.bin     # Use custom input file
 *   node benchmark.js --output out.tamp    # Save compressed output to file
 *   node benchmark.js --verify             # Full byte-by-byte verification
 *   node benchmark.js --save results.json  # Save timing results to JSON
 *   node benchmark.js --compare old.json   # Compare against previous results
 */

import { compress, decompress } from './dist/index.mjs';
import { readFileSync, writeFileSync, existsSync } from 'fs';
import { performance } from 'perf_hooks';
import { parseArgs } from 'util';
import { basename } from 'path';

// Default configuration
const DEFAULT_INPUT = '../datasets/enwik8';
const DEFAULT_ITERATIONS = 3;
const WARMUP_ITERATIONS = 1;

/**
 * Parse command line arguments
 */
function parseArguments() {
  const options = {
    iterations: { type: 'string', short: 'n', default: String(DEFAULT_ITERATIONS) },
    input: { type: 'string', short: 'i', default: DEFAULT_INPUT },
    output: { type: 'string', short: 'o' },
    verify: { type: 'boolean', short: 'v', default: false },
    save: { type: 'string', short: 's' },
    compare: { type: 'string', short: 'c' },
    help: { type: 'boolean', short: 'h', default: false },
  };

  try {
    const { values } = parseArgs({ options, allowPositionals: true });

    if (values.help) {
      console.log(`
Tamp JavaScript Bindings Benchmark

Usage: node benchmark.js [options]

Options:
  -n, --iterations <N>   Number of iterations (default: ${DEFAULT_ITERATIONS})
  -i, --input <file>     Input file to compress (default: ${DEFAULT_INPUT})
  -o, --output <file>    Save compressed data to file
  -v, --verify           Full byte-by-byte verification of decompressed data
  -s, --save <file>      Save timing results to JSON file
  -c, --compare <file>   Compare results against previous JSON file
  -h, --help             Show this help message

Examples:
  node benchmark.js
  node benchmark.js --iterations 5 --save results.json
  node benchmark.js --compare baseline.json
  node benchmark.js --output compressed.tamp --verify
`);
      process.exit(0);
    }

    return {
      iterations: parseInt(values.iterations, 10),
      inputFile: values.input,
      outputFile: values.output,
      verify: values.verify,
      saveFile: values.save,
      compareFile: values.compare,
    };
  } catch (err) {
    console.error('Error parsing arguments:', err.message);
    process.exit(1);
  }
}

/**
 * Calculate statistics from an array of measurements
 */
function calculateStats(times) {
  const sorted = [...times].sort((a, b) => a - b);
  const sum = sorted.reduce((a, b) => a + b, 0);
  const mean = sum / sorted.length;
  const median = sorted[Math.floor(sorted.length / 2)];
  const min = sorted[0];
  const max = sorted[sorted.length - 1];

  // Standard deviation
  const squaredDiffs = sorted.map(t => Math.pow(t - mean, 2));
  const avgSquaredDiff = squaredDiffs.reduce((a, b) => a + b, 0) / sorted.length;
  const stdDev = Math.sqrt(avgSquaredDiff);

  return { mean, median, min, max, stdDev, samples: sorted.length };
}

/**
 * Format milliseconds with appropriate precision
 */
function formatMs(ms) {
  if (ms < 1) return `${(ms * 1000).toFixed(2)}us`;
  if (ms < 1000) return `${ms.toFixed(2)}ms`;
  return `${(ms / 1000).toFixed(2)}s`;
}

/**
 * Format bytes as human-readable
 */
function formatBytes(bytes) {
  if (bytes >= 1048576) return `${(bytes / 1048576).toFixed(2)} MB`;
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(2)} KB`;
  return `${bytes} B`;
}

/**
 * Format throughput as MB/s
 */
function formatThroughput(bytes, ms) {
  const mbPerSec = bytes / 1024 / 1024 / (ms / 1000);
  return `${mbPerSec.toFixed(2)} MB/s`;
}

/**
 * Verify two Uint8Arrays are identical
 */
function verifyData(original, decompressed) {
  if (original.length !== decompressed.length) {
    return { success: false, message: `Length mismatch: ${original.length} vs ${decompressed.length}` };
  }

  for (let i = 0; i < original.length; i++) {
    if (original[i] !== decompressed[i]) {
      return { success: false, message: `Byte mismatch at position ${i}: ${original[i]} vs ${decompressed[i]}` };
    }
  }

  return { success: true };
}

/**
 * Run compression benchmark
 */
async function benchmarkCompress(data, iterations) {
  const times = [];

  // Warmup
  for (let i = 0; i < WARMUP_ITERATIONS; i++) {
    await compress(data);
  }

  // Timed runs
  let lastResult;
  for (let i = 0; i < iterations; i++) {
    const start = performance.now();
    lastResult = await compress(data);
    const elapsed = performance.now() - start;
    times.push(elapsed);
    process.stdout.write(`\r  Compression: ${i + 1}/${iterations}`);
  }
  console.log();

  return {
    stats: calculateStats(times),
    inputSize: data.length,
    outputSize: lastResult.length,
    ratio: lastResult.length / data.length,
    compressedData: lastResult,
  };
}

/**
 * Run decompression benchmark
 */
async function benchmarkDecompress(compressedData, expectedSize, iterations) {
  const times = [];

  // Warmup
  for (let i = 0; i < WARMUP_ITERATIONS; i++) {
    await decompress(compressedData);
  }

  // Timed runs
  let lastResult;
  for (let i = 0; i < iterations; i++) {
    const start = performance.now();
    lastResult = await decompress(compressedData);
    const elapsed = performance.now() - start;
    times.push(elapsed);
    process.stdout.write(`\r  Decompression: ${i + 1}/${iterations}`);
  }
  console.log();

  // Verify output size
  if (lastResult.length !== expectedSize) {
    throw new Error(`Decompression size mismatch: expected ${expectedSize}, got ${lastResult.length}`);
  }

  return {
    stats: calculateStats(times),
    inputSize: compressedData.length,
    outputSize: lastResult.length,
    decompressedData: lastResult,
  };
}

/**
 * Print results table
 */
function printResults(compressResult, decompressResult, inputFile) {
  console.log('\n' + '='.repeat(60));
  console.log('BENCHMARK RESULTS');
  console.log('='.repeat(60));

  console.log(`\nInput: ${inputFile}`);
  console.log(`Original size:   ${formatBytes(compressResult.inputSize)}`);
  console.log(`Compressed size: ${formatBytes(compressResult.outputSize)}`);
  console.log(`Ratio:           ${(compressResult.ratio * 100).toFixed(2)}%`);

  console.log('\n--- Compression ---');
  console.log(`  Mean:       ${formatMs(compressResult.stats.mean)}`);
  console.log(`  Median:     ${formatMs(compressResult.stats.median)}`);
  console.log(`  Min:        ${formatMs(compressResult.stats.min)}`);
  console.log(`  Max:        ${formatMs(compressResult.stats.max)}`);
  console.log(`  Std Dev:    ${formatMs(compressResult.stats.stdDev)}`);
  console.log(`  Throughput: ${formatThroughput(compressResult.inputSize, compressResult.stats.mean)}`);

  console.log('\n--- Decompression ---');
  console.log(`  Mean:       ${formatMs(decompressResult.stats.mean)}`);
  console.log(`  Median:     ${formatMs(decompressResult.stats.median)}`);
  console.log(`  Min:        ${formatMs(decompressResult.stats.min)}`);
  console.log(`  Max:        ${formatMs(decompressResult.stats.max)}`);
  console.log(`  Std Dev:    ${formatMs(decompressResult.stats.stdDev)}`);
  console.log(`  Throughput: ${formatThroughput(decompressResult.outputSize, decompressResult.stats.mean)}`);

  console.log('\n' + '='.repeat(60));
}

/**
 * Compare results against baseline
 */
function compareResults(current, baseline) {
  console.log('\n' + '='.repeat(60));
  console.log('COMPARISON WITH BASELINE');
  console.log('='.repeat(60));

  const compSpeedup = baseline.compression.stats.mean / current.compression.stats.mean;
  const decompSpeedup = baseline.decompression.stats.mean / current.decompression.stats.mean;

  console.log('\n--- Compression ---');
  console.log(`  Baseline:  ${formatMs(baseline.compression.stats.mean)}`);
  console.log(`  Current:   ${formatMs(current.compression.stats.mean)}`);
  console.log(`  Change:    ${compSpeedup >= 1 ? '+' : ''}${((compSpeedup - 1) * 100).toFixed(1)}%`);
  console.log(`  Speedup:   ${compSpeedup.toFixed(2)}x`);

  console.log('\n--- Decompression ---');
  console.log(`  Baseline:  ${formatMs(baseline.decompression.stats.mean)}`);
  console.log(`  Current:   ${formatMs(current.decompression.stats.mean)}`);
  console.log(`  Change:    ${decompSpeedup >= 1 ? '+' : ''}${((decompSpeedup - 1) * 100).toFixed(1)}%`);
  console.log(`  Speedup:   ${decompSpeedup.toFixed(2)}x`);

  console.log('\n' + '='.repeat(60));

  return { compSpeedup, decompSpeedup };
}

/**
 * Main benchmark function
 */
async function main() {
  const args = parseArguments();

  console.log('Tamp JavaScript Bindings Benchmark');
  console.log('===================================\n');

  // Check input file exists
  if (!existsSync(args.inputFile)) {
    console.error(`Error: Input file not found: ${args.inputFile}`);
    console.error('\nTo download enwik8:');
    console.error('  mkdir -p ../datasets');
    console.error('  curl -o ../datasets/enwik8.zip http://mattmahoney.net/dc/enwik8.zip');
    console.error('  unzip ../datasets/enwik8.zip -d ../datasets/');
    process.exit(1);
  }

  // Load input data
  console.log(`Loading input file: ${args.inputFile}`);
  const inputData = new Uint8Array(readFileSync(args.inputFile));
  console.log(`Input size: ${formatBytes(inputData.length)}`);
  console.log(`Iterations: ${args.iterations} (+ ${WARMUP_ITERATIONS} warmup)`);
  if (args.verify) console.log('Verification: enabled');
  console.log();

  // Run compression benchmark
  console.log('Running compression benchmark...');
  const compressResult = await benchmarkCompress(inputData, args.iterations);

  // Save compressed output if requested
  if (args.outputFile) {
    writeFileSync(args.outputFile, compressResult.compressedData);
    console.log(`Compressed data saved to: ${args.outputFile}`);
  }

  // Run decompression benchmark
  console.log('Running decompression benchmark...');
  const decompressResult = await benchmarkDecompress(compressResult.compressedData, inputData.length, args.iterations);

  // Full verification if requested
  if (args.verify) {
    process.stdout.write('Verifying decompressed data... ');
    const verifyResult = verifyData(inputData, decompressResult.decompressedData);
    if (verifyResult.success) {
      console.log('OK');
    } else {
      console.log('FAILED');
      console.error(`Verification error: ${verifyResult.message}`);
      process.exit(1);
    }
  }

  // Print results
  printResults(compressResult, decompressResult, args.inputFile);

  // Prepare results object for saving/comparison
  const results = {
    timestamp: new Date().toISOString(),
    inputFile: basename(args.inputFile),
    inputSize: inputData.length,
    compressedSize: compressResult.outputSize,
    iterations: args.iterations,
    compression: {
      stats: compressResult.stats,
      throughputMBps: inputData.length / 1024 / 1024 / (compressResult.stats.mean / 1000),
    },
    decompression: {
      stats: decompressResult.stats,
      throughputMBps: inputData.length / 1024 / 1024 / (decompressResult.stats.mean / 1000),
    },
    system: {
      nodeVersion: process.version,
      platform: process.platform,
      arch: process.arch,
    },
  };

  // Compare against baseline if provided
  if (args.compareFile) {
    if (!existsSync(args.compareFile)) {
      console.error(`\nWarning: Comparison file not found: ${args.compareFile}`);
    } else {
      const baseline = JSON.parse(readFileSync(args.compareFile, 'utf-8'));
      compareResults(results, baseline);
    }
  }

  // Save results if requested
  if (args.saveFile) {
    writeFileSync(args.saveFile, JSON.stringify(results, null, 2));
    console.log(`\nResults saved to: ${args.saveFile}`);
  }

  // Print machine-readable summary for CI
  console.log('\n--- Machine-Readable Summary ---');
  console.log(`COMPRESS_MEAN_MS=${compressResult.stats.mean.toFixed(2)}`);
  console.log(`COMPRESS_THROUGHPUT_MBPS=${results.compression.throughputMBps.toFixed(2)}`);
  console.log(`DECOMPRESS_MEAN_MS=${decompressResult.stats.mean.toFixed(2)}`);
  console.log(`DECOMPRESS_THROUGHPUT_MBPS=${results.decompression.throughputMBps.toFixed(2)}`);
}

main().catch(err => {
  console.error('\nBenchmark failed:', err.message);
  if (err.stack) {
    console.error(err.stack);
  }
  process.exit(1);
});
