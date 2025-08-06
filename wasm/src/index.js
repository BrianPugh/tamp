/**
 * Tamp WebAssembly - Main entry point
 * Export all public APIs
 */

// Core compression/decompression
export {
  TampCompressor,
  TampDecompressor,
  compress,
  decompress,
  compressText,
  decompressText,
  initialize,
  initializeDictionary,
  computeMinPatternSize,
  using,
  TampError,
  ExcessBitsError,
  CompressionError,
  DecompressionError,
} from './tamp.js';

// Streaming APIs
export {
  TampCompressionStream,
  TampDecompressionStream,
  compressStream,
  decompressStream,
  createReadableStream,
  collectStream,
} from './streams.js';

// Version info
export const version = '1.0.0';
