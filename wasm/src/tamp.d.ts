/**
 * TypeScript type definitions for Tamp WebAssembly
 */

/**
 * Progress information object passed to progress callbacks
 */
export interface ProgressInfo {
  /** Number of input bytes processed */
  bytesProcessed: number;
  /** Total number of input bytes */
  totalBytes: number;
  /** Completion percentage (0-100) */
  percent: number;
  /** Processing speed in bytes/second */
  bytesPerSecond: number;
  /** Estimated seconds remaining */
  estimatedTimeRemaining: number;
  /** Number of chunks processed */
  chunksProcessed: number;
  /** Total elapsed time in seconds */
  elapsedTime: number;
  /** Size of current chunk being processed */
  chunkSize: number;
  /** Size of output written for current chunk */
  outputSize: number;
}

/**
 * Progress callback function for compression operations
 * @param progressInfo - Rich progress information object
 * @throws Error to abort compression
 */
export type TampProgressCallback = (progressInfo: ProgressInfo) => void;

/**
 * Configuration options for Tamp compression/decompression
 */
export interface TampOptions {
  /** Number of window bits (8-15). Default: 10 */
  window?: number;
  /** Number of literal bits (5-8). Default: 8 */
  literal?: number;
  /** Custom dictionary data. If null, no custom dictionary is used. If Uint8Array, uses the provided dictionary. Default: null */
  dictionary?: Uint8Array | null;
  /** Enable extended format (RLE, extended match) for better compression ratios. Default: true */
  extended?: boolean;
  /** Enable lazy matching for better compression ratios. Default: false */
  lazy_matching?: boolean;
}

/**
 * Extended options interface for functions that support progress callbacks and cancellation
 */
export interface TampCallbackOptions extends TampOptions {
  /** Progress callback with rich progress info */
  onPoll?: TampProgressCallback;
  /** AbortSignal for cancellation */
  signal?: AbortSignal;
  /** Minimum interval between progress callbacks in milliseconds. Default: 100 */
  pollIntervalMs?: number;
  /** Minimum bytes processed between progress callbacks. Default: 65536 */
  pollIntervalBytes?: number;
}

/**
 * Default configuration values
 */
export interface TampDefaults {
  readonly window: 10;
  readonly literal: 8;
  readonly dictionary: null;
  readonly extended: true;
  readonly lazy_matching: false;
}

/**
 * Base Tamp error with error code and details
 */
export class TampError extends Error {
  readonly name: 'TampError';
  readonly code: number;
  readonly details: Record<string, any>;

  constructor(code: number, message: string, details?: Record<string, any>);
}

/**
 * Error thrown when data has more bits than expected literal size
 */
export class ExcessBitsError extends TampError {
  readonly name: 'ExcessBitsError';

  constructor(message?: string, details?: Record<string, any>);
}

/**
 * Error thrown during compression operations
 */
export class CompressionError extends TampError {
  readonly name: 'CompressionError';

  constructor(message?: string, details?: Record<string, any>);
}

/**
 * Error thrown during decompression operations
 */
export class DecompressionError extends TampError {
  readonly name: 'DecompressionError';

  constructor(message?: string, details?: Record<string, any>);
}

/**
 * Tamp Compressor class for streaming compression
 */
export class TampCompressor {
  /**
   * Create a new Tamp compressor
   * @param options - Compression configuration options
   */
  constructor(options?: TampOptions);

  /**
   * Compress a chunk of data
   * @param input - Input data to compress
   * @param options - Options for progress callback and cancellation
   * @returns Promise resolving to compressed data
   */
  compress(input: Uint8Array, options?: TampCompressOptions): Promise<Uint8Array>;

  /**
   * Flush any remaining data and finalize compression
   * @returns Promise resolving to final compressed output
   */
  flush(): Promise<Uint8Array>;

  /**
   * Clean up allocated memory. Should be called when done with the compressor.
   */
  destroy(): void;
}

/**
 * Options for the TampCompressor.compress() method (not the one-shot compress function)
 */
export interface TampCompressOptions {
  /** Progress callback with rich progress info */
  onPoll?: TampProgressCallback;
  /** AbortSignal for cancellation */
  signal?: AbortSignal;
  /** Minimum interval between progress callbacks in milliseconds. Default: 100 */
  pollIntervalMs?: number;
  /** Minimum bytes processed between progress callbacks. Default: 65536 */
  pollIntervalBytes?: number;
}

/**
 * Options for decompression operations that support cancellation
 */
export interface TampDecompressOptions {
  /** AbortSignal for cancellation */
  signal?: AbortSignal;
}

/**
 * Tamp Decompressor class for streaming decompression
 */
export class TampDecompressor {
  /**
   * Create a new Tamp decompressor
   * @param options - Decompression configuration options
   */
  constructor(options?: TampOptions);

  /**
   * Decompress a chunk of data
   * @param input - Compressed input data
   * @param options - Decompression options (e.g., AbortSignal for cancellation)
   * @returns Promise resolving to decompressed data
   * @throws {RangeError} When compressed data contains out-of-bounds references
   * @throws {DecompressionError} When decompression fails or is aborted
   */
  decompress(input: Uint8Array, options?: TampDecompressOptions): Promise<Uint8Array>;

  /**
   * Clean up allocated memory. Should be called when done with the decompressor.
   */
  destroy(): void;
}

/**
 * Transform stream for compression
 */
export class TampCompressionStream extends TransformStream<Uint8Array, Uint8Array> {
  /**
   * Create a new compression transform stream
   * @param options - Compression configuration options
   */
  constructor(options?: TampOptions);
}

/**
 * Transform stream for decompression
 */
export class TampDecompressionStream extends TransformStream<Uint8Array, Uint8Array> {
  /**
   * Create a new decompression transform stream
   * @param options - Decompression configuration options
   */
  constructor(options?: TampOptions);
}

/**
 * One-shot compression function
 * @param data - Data to compress
 * @param options - Compression options with optional progress callback
 * @returns Promise resolving to compressed data
 */
export function compress(data: Uint8Array, options?: TampCallbackOptions): Promise<Uint8Array>;

/**
 * One-shot decompression function
 * @param data - Compressed data to decompress
 * @param options - Decompression options
 * @returns Promise resolving to decompressed data
 * @throws {RangeError} When compressed data contains out-of-bounds references or invalid configuration
 * @throws {DecompressionError} When decompression fails
 */
export function decompress(data: Uint8Array, options?: TampOptions): Promise<Uint8Array>;

/**
 * Initialize the WebAssembly module (called automatically, but can be called explicitly for preloading)
 * @returns Promise that resolves when the module is ready
 */
export function initialize(): Promise<void>;

/**
 * Initialize a dictionary buffer with default values
 * @param size - Size of the dictionary buffer (must be power of 2)
 * @returns Promise resolving to initialized dictionary buffer
 */
export function initializeDictionary(size: number): Promise<Uint8Array>;

/**
 * Compute the minimum pattern size for given window and literal parameters
 * @param window - Number of window bits (8-15)
 * @param literal - Number of literal bits (5-8)
 * @returns Promise resolving to minimum pattern size in bytes (2 or 3)
 */
export function computeMinPatternSize(window: number, literal: number): Promise<number>;

/**
 * Compress text string to bytes
 * @param text - Text string to compress
 * @param options - Compression options with optional progress callback
 * @returns Promise resolving to compressed data
 */
export function compressText(text: string, options?: TampCallbackOptions): Promise<Uint8Array>;

/**
 * Decompress bytes to text string
 * @param data - Compressed data to decompress
 * @param options - Decompression options
 * @param encoding - Text encoding to use (default: 'utf-8')
 * @returns Promise resolving to decompressed text
 */
export function decompressText(data: Uint8Array, options?: TampOptions, encoding?: string): Promise<string>;

/**
 * Automatic resource management helper
 * @param resource - Resource to manage (must have destroy() method)
 * @param fn - Function to execute with the resource
 * @returns Promise resolving to the result of the function
 */
export function using<T extends { destroy(): void }, R>(resource: T, fn: (resource: T) => Promise<R> | R): Promise<R>;

/**
 * Convenience function to compress a stream
 * @param readable - Input stream
 * @param options - Compression options
 * @returns Compressed output stream
 */
export function compressStream(readable: ReadableStream<Uint8Array>, options?: TampOptions): ReadableStream<Uint8Array>;

/**
 * Convenience function to decompress a stream
 * @param readable - Compressed input stream
 * @param options - Decompression options
 * @returns Decompressed output stream
 */
export function decompressStream(
  readable: ReadableStream<Uint8Array>,
  options?: TampOptions
): ReadableStream<Uint8Array>;

/**
 * Helper function to convert a Uint8Array to a ReadableStream
 * @param data - Data to convert
 * @param chunkSize - Size of each chunk (default: 8192)
 * @returns ReadableStream
 */
export function createReadableStream(data: Uint8Array, chunkSize?: number): ReadableStream<Uint8Array>;

/**
 * Helper function to collect a ReadableStream into a Uint8Array
 * @param readable - Stream to collect
 * @returns Promise resolving to collected data
 */
export function collectStream(readable: ReadableStream<Uint8Array>): Promise<Uint8Array>;

/**
 * Version information
 */
export const version: string;
