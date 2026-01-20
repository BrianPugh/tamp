/**
 * Tamp WebAssembly JavaScript Wrapper
 * High-level JavaScript API for Tamp compression/decompression
 */

import TampModule from './tamp-module.mjs';

// Error codes from C library
const TAMP_ERROR = -1;
const TAMP_EXCESS_BITS = -2;
const TAMP_INVALID_CONF = -3;
const TAMP_OOB = -4;

class TampError extends Error {
  constructor(code, message, details = {}) {
    super(message);
    this.name = 'TampError';
    this.code = code;
    this.details = details;
  }
}

class ExcessBitsError extends TampError {
  constructor(message = 'Provided data has more bits than expected literal bits', details = {}) {
    super(-2, message, details);
    this.name = 'ExcessBitsError';
  }
}

class CompressionError extends TampError {
  constructor(message = 'Compression operation failed', details = {}) {
    super(-1, message, details);
    this.name = 'CompressionError';
  }
}

class DecompressionError extends TampError {
  constructor(message = 'Decompression operation failed', details = {}) {
    super(-1, message, details);
    this.name = 'DecompressionError';
  }
}

/**
 * WebAssembly module singleton with proper initialization
 */
class WasmModuleManager {
  #module = null;
  #initPromise = null;
  #structSizes = null;

  async initialize() {
    if (!this.#initPromise) {
      this.#initPromise = this.#doInitialize();
    }
    return await this.#initPromise;
  }

  async #doInitialize() {
    this.#module = await TampModule();

    // Query struct sizes from C code once at initialization
    this.#structSizes = {
      compressor: this.#module.ccall('tamp_compressor_sizeof', 'number', [], []),
      decompressor: this.#module.ccall('tamp_decompressor_sizeof', 'number', [], []),
      conf: this.#module.ccall('tamp_conf_sizeof', 'number', [], []),
    };

    return this.#module;
  }

  get module() {
    return this.#module;
  }

  get structSizes() {
    return this.#structSizes;
  }

  get isInitialized() {
    return this.#module !== null;
  }
}

const wasmManager = new WasmModuleManager();

async function initializeWasm() {
  return await wasmManager.initialize();
}

function throwOnError(result, operation) {
  if (result < 0) {
    const details = { operation, code: result };

    switch (result) {
      case TAMP_EXCESS_BITS:
        throw new ExcessBitsError(`${operation}: Symbol has more bits than configured literal size`, details);
      case TAMP_INVALID_CONF:
        throw new RangeError(`${operation}: Invalid configuration parameters`);
      case TAMP_OOB:
        throw new RangeError(`${operation}: Out-of-bounds access detected in compressed data`);
      case TAMP_ERROR:
      default:
        if (operation.toLowerCase().includes('compress')) {
          throw new CompressionError(`${operation}: Operation failed`, details);
        } else if (operation.toLowerCase().includes('decompress')) {
          throw new DecompressionError(`${operation}: Operation failed`, details);
        } else {
          throw new TampError(result, `${operation}: Unknown error`, details);
        }
    }
  }
  return result;
}

/**
 * Tamp Compressor class for streaming compression
 */
export class TampCompressor {
  constructor(options = {}) {
    this.options = {
      window: 10,
      literal: 8,
      dictionary: null,
      lazy_matching: false,
      ...options,
    };

    // Validate window and literal ranges
    if (this.options.window < 8 || this.options.window > 15) {
      throw new RangeError(`window must be between 8 and 15, got ${this.options.window}`);
    }
    if (this.options.literal < 5 || this.options.literal > 8) {
      throw new RangeError(`literal must be between 5 and 8, got ${this.options.literal}`);
    }

    this.compressorPtr = null;
    this.windowPtr = null;
    this.module = null;
    this._initPromise = null;
  }

  async initialize() {
    if (!this._initPromise) {
      this._initPromise = this._doInitialize();
    }
    return this._initPromise;
  }

  async _doInitialize() {
    this.module = await initializeWasm();
    const windowSize = 1 << this.options.window;

    // Validate dictionary size before allocating
    const dictData = this.options.dictionary ? new Uint8Array(this.options.dictionary) : null;
    if (dictData && dictData.length !== windowSize) {
      throw new RangeError(`Dictionary size (${dictData.length}) must match window size (${windowSize})`);
    }

    const { compressor: compressorStructSize, conf: confStructSize } = wasmManager.structSizes;

    // Allocate memory for compressor struct, config struct, and window buffer in single allocation
    const totalSize = compressorStructSize + confStructSize + windowSize;
    this.compressorPtr = this.module._malloc(totalSize);
    if (this.compressorPtr === 0) {
      throw new RangeError(`Failed to allocate memory for compressor (${totalSize} bytes)`);
    }

    const confPtr = this.compressorPtr + compressorStructSize;
    this.windowPtr = confPtr + confStructSize;

    // Initialize dictionary
    if (dictData) {
      this.module.HEAPU8.set(dictData, this.windowPtr);
    } else {
      this.module.ccall('tamp_initialize_dictionary', null, ['number', 'number'], [this.windowPtr, windowSize]);
    }

    // Create configuration struct (already allocated as part of single allocation)

    try {
      const confValue =
        (this.options.window & 0xf) |
        ((this.options.literal & 0xf) << 4) |
        ((this.options.dictionary ? 1 : 0) << 8) |
        ((this.options.lazy_matching ? 1 : 0) << 9);
      this.module.setValue(confPtr, confValue, 'i32');

      // Initialize compressor
      const result = this.module.ccall(
        'tamp_compressor_init',
        'number',
        ['number', 'number', 'number'],
        [this.compressorPtr, confPtr, this.windowPtr]
      );

      throwOnError(result, 'Compressor initialization');
    } catch (error) {
      this.module._free(this.compressorPtr); // Frees struct, window buffer, and config
      this.compressorPtr = null;
      this.windowPtr = null;
      throw error;
    }
  }

  /**
   * Compress data chunk
   * @param {Uint8Array} input - Input data to compress
   * @param {Object} [options] - Options object
   * @param {Function} [options.onPoll] - Progress callback
   * @param {AbortSignal} [options.signal] - AbortSignal for cancellation
   * @param {number} [options.pollIntervalMs=100] - Minimum interval between progress callbacks in milliseconds
   * @param {number} [options.pollIntervalBytes=65536] - Minimum bytes processed between progress callbacks
   * @returns {Promise<Uint8Array>} - Compressed output
   */
  async compress(input, options = {}) {
    const progressCallback = options.onPoll;
    await this.initialize();

    if (!this.compressorPtr) {
      throw new Error('Compressor has been destroyed');
    }

    const CHUNK_SIZE = 1 << 20;
    const outputChunks = [];

    // Extract options
    const { signal, pollIntervalMs = 100, pollIntervalBytes = 65536 } = options;

    // Progress tracking
    const startTime = performance.now();
    let lastProgressTime = startTime;
    let lastProgressBytes = 0;
    let totalChunksProcessed = 0;

    // Check for cancellation
    const checkAborted = () => {
      if (signal?.aborted) {
        throw new CompressionError('Compression was aborted', {
          aborted: true,
          reason: signal.reason,
        });
      }
    };

    // Single allocation for input buffer + output buffer + four 4-byte pointers
    const totalAllocSize = CHUNK_SIZE + CHUNK_SIZE + 16;
    const basePtr = this.module._malloc(totalAllocSize);
    if (basePtr === 0) {
      throw new RangeError(`Failed to allocate memory (${totalAllocSize} bytes)`);
    }

    const inputPtr = basePtr;
    const outputPtr = inputPtr + CHUNK_SIZE;
    const outputSizePtr = outputPtr + CHUNK_SIZE;
    const inputConsumedPtr = outputSizePtr + 4;
    const sinkConsumedPtr = inputConsumedPtr + 4;
    const pollOutputSizePtr = sinkConsumedPtr + 4;
    // No need for callback function pointer setup since we're calling JS callback directly

    try {
      checkAborted(); // Check for cancellation before we start

      let inputRemaining = input.length;
      let inputOffset = 0;

      while (inputRemaining > 0) {
        checkAborted(); // Check for cancellation at start of each chunk
        const currentInputSize = Math.min(inputRemaining, CHUNK_SIZE);

        // Copy input chunk to WASM memory
        this.module.HEAPU8.set(input.subarray(inputOffset, inputOffset + currentInputSize), inputPtr);

        // Use lower-level API instead of tamp_compressor_compress_cb
        // This is a translation of the C implementation
        let chunkInputPtr = inputPtr;
        let chunkInputSize = currentInputSize;
        let chunkInputConsumed = 0;
        let chunkOutputWritten = 0;

        // Main compression loop - equivalent to the while loop in tamp_compressor_compress_cb
        while (chunkInputSize > 0 && CHUNK_SIZE - chunkOutputWritten > 0) {
          // Sink data into compressor's internal buffer
          this.module.ccall(
            'tamp_compressor_sink',
            null,
            ['number', 'number', 'number', 'number'],
            [this.compressorPtr, chunkInputPtr, chunkInputSize, sinkConsumedPtr]
          );

          const sinkConsumed = this.module.getValue(sinkConsumedPtr, 'i32');
          chunkInputPtr += sinkConsumed;
          chunkInputSize -= sinkConsumed;
          chunkInputConsumed += sinkConsumed;

          // Check if compressor internal buffer is full
          const inputBufferIsFull = this.module.ccall(
            'tamp_compressor_full',
            'number',
            ['number'],
            [this.compressorPtr]
          );

          if (inputBufferIsFull) {
            // Perform 1 compression cycle.
            const pollResult = this.module.ccall(
              'tamp_compressor_poll',
              'number',
              ['number', 'number', 'number', 'number'],
              [this.compressorPtr, outputPtr + chunkOutputWritten, CHUNK_SIZE - chunkOutputWritten, pollOutputSizePtr]
            );

            if (pollResult !== 0) {
              throwOnError(pollResult, 'Compression poll');
            }

            const pollOutputSize = this.module.getValue(pollOutputSizePtr, 'i32');
            chunkOutputWritten += pollOutputSize;

            // Call progress callback if provided (after successful compression)
            // Use throttling to reduce callback overhead
            if (progressCallback && typeof progressCallback === 'function') {
              const currentTime = performance.now();
              const bytesProcessed = inputOffset + chunkInputConsumed;
              const timeSinceLastCallback = currentTime - lastProgressTime;
              const bytesSinceLastCallback = bytesProcessed - lastProgressBytes;

              // Only call callback if enough time has passed OR enough bytes processed
              const shouldCallback =
                timeSinceLastCallback >= pollIntervalMs ||
                bytesSinceLastCallback >= pollIntervalBytes ||
                bytesProcessed === input.length; // Always call on completion

              if (shouldCallback) {
                checkAborted(); // Check for cancellation before callback

                const elapsedSeconds = (currentTime - startTime) / 1000;
                const bytesPerSecond = elapsedSeconds > 0 ? bytesProcessed / elapsedSeconds : 0;
                const progressPercent = input.length > 0 ? (bytesProcessed / input.length) * 100 : 0;
                const remainingBytes = input.length - bytesProcessed;
                const estimatedTimeRemaining = bytesPerSecond > 0 ? remainingBytes / bytesPerSecond : 0;

                // Progress callback: (progressInfo) => void | boolean
                const progressInfo = {
                  bytesProcessed,
                  totalBytes: input.length,
                  percent: progressPercent,
                  bytesPerSecond: Math.round(bytesPerSecond),
                  estimatedTimeRemaining,
                  chunksProcessed: totalChunksProcessed,
                  elapsedTime: elapsedSeconds,
                  chunkSize: pollOutputSize,
                  outputSize: chunkOutputWritten,
                };

                await Promise.resolve(progressCallback(progressInfo));

                lastProgressTime = currentTime;
                lastProgressBytes = bytesProcessed;
              }
            }

            // Check for cancellation after callback (in case callback triggered abort)
            checkAborted();

            totalChunksProcessed++;
          }
        }

        // Set the final values
        this.module.setValue(outputSizePtr, chunkOutputWritten, 'i32');
        this.module.setValue(inputConsumedPtr, chunkInputConsumed, 'i32');

        if (chunkOutputWritten > 0) {
          const outputChunk = new Uint8Array(this.module.HEAPU8.buffer, outputPtr, chunkOutputWritten).slice();
          outputChunks.push(outputChunk);
        }

        // Update remaining input
        inputOffset += chunkInputConsumed;
        inputRemaining -= chunkInputConsumed;

        // Safety check: if no input was consumed, break to avoid infinite loop
        if (chunkInputConsumed === 0 && inputRemaining > 0) {
          throw new CompressionError('No input consumed in compression loop', {
            inputRemaining,
            inputConsumed: chunkInputConsumed,
          });
        }
      }

      // Concatenate all output chunks
      const totalSize = outputChunks.reduce((sum, chunk) => sum + chunk.length, 0);
      const result = new Uint8Array(totalSize);
      let offset = 0;
      for (const chunk of outputChunks) {
        result.set(chunk, offset);
        offset += chunk.length;
      }

      return result;
    } finally {
      this.module._free(basePtr);
    }
  }

  /**
   * Flush any remaining data and finalize compression
   * @param {boolean} [write_token=false] - Whether to write a token during flush
   * @returns {Promise<Uint8Array>} - Final compressed output
   */
  async flush(write_token = false) {
    await this.initialize();

    if (!this.compressorPtr) {
      throw new Error('Compressor has been destroyed');
    }

    // Single allocation for output buffer (32 bytes) + output size pointer (4 bytes)
    const totalAllocSize = 32 + 4;
    const basePtr = this.module._malloc(totalAllocSize);
    if (basePtr === 0) {
      throw new RangeError(`Failed to allocate flush memory (${totalAllocSize} bytes)`);
    }

    const outputPtr = basePtr;
    const outputSizePtr = basePtr + 32;

    try {
      const result = this.module.ccall(
        'tamp_compressor_flush',
        'number',
        ['number', 'number', 'number', 'number', 'number'],
        [this.compressorPtr, outputPtr, 32, outputSizePtr, write_token ? 1 : 0]
      );

      throwOnError(result, 'Flush');

      const outputSize = this.module.getValue(outputSizePtr, 'i32');
      const flushed = new Uint8Array(this.module.HEAPU8.buffer, outputPtr, outputSize).slice();
      return flushed;
    } finally {
      this.module._free(basePtr);
    }
  }

  /**
   * Clean up allocated memory
   * @returns {void}
   */
  destroy() {
    if (this.compressorPtr) {
      this.module._free(this.compressorPtr); // Frees compressor struct, config struct, and window buffer
      this.compressorPtr = null;
      this.windowPtr = null; // Don't free separately - part of same allocation
    }
  }
}

/**
 * Tamp Decompressor class for streaming decompression
 */
export class TampDecompressor {
  constructor(options = {}) {
    this.options = {
      window: 10,
      literal: 8,
      dictionary: null,
      lazy_matching: false,
      ...options,
    };

    // Validate window and literal ranges (if provided, these are used for dictionary matching)
    if (this.options.window < 8 || this.options.window > 15) {
      throw new RangeError(`window must be between 8 and 15, got ${this.options.window}`);
    }
    if (this.options.literal < 5 || this.options.literal > 8) {
      throw new RangeError(`literal must be between 5 and 8, got ${this.options.literal}`);
    }

    this.decompressorPtr = null;
    this.windowPtr = null;
    this.module = null;
    this._initPromise = null;
    this.initialPtr = null;
    this.confPtr = null;
    this.inputConsumedPtr = null;
    this.headerInputPtr = null;
    // State tracking for streaming
    this.headerRead = false;
    this.decompressorInitialized = false;
    this.pendingInput = new Uint8Array(0);
  }

  async initialize() {
    if (!this._initPromise) {
      this._initPromise = this._doInitialize();
    }
    return this._initPromise;
  }

  async _doInitialize() {
    this.module = await initializeWasm();

    const { conf: confStructSize } = wasmManager.structSizes;

    // Allocate memory for header processing: conf + inputConsumed(4) + headerInput(1)
    const initialSize = confStructSize + 4 + 1;
    this.initialPtr = this.module._malloc(initialSize);
    if (this.initialPtr === 0) {
      throw new RangeError(`Failed to allocate initial memory (${initialSize} bytes)`);
    }

    this.confPtr = this.initialPtr;
    this.inputConsumedPtr = this.initialPtr + confStructSize;
    this.headerInputPtr = this.inputConsumedPtr + 4;
  }

  /**
   * Read and process the header from compressed data.
   * @param {Uint8Array} input - Input data containing the header
   * @returns {Promise<number>} - Number of bytes consumed from input
   */
  async _readHeader(input) {
    if (this.headerRead) {
      return 0; // Header already read
    }

    if (input.length === 0) {
      return 0; // No data to process
    }

    // Copy first byte for header reading
    this.module.setValue(this.headerInputPtr, input[0], 'i8');

    // Read header to get configuration
    const headerResult = this.module.ccall(
      'tamp_decompressor_read_header',
      'number',
      ['number', 'number', 'number', 'number'],
      [this.confPtr, this.headerInputPtr, 1, this.inputConsumedPtr]
    );

    throwOnError(headerResult, 'Header reading');

    // Extract configuration values from the header
    const confValue = this.module.getValue(this.confPtr, 'i32');
    const headerWindow = confValue & 0xf;
    // const headerLiteral = (confValue >> 4) & 0xf;  // Currently unused
    const headerUsesCustomDict = (confValue >> 8) & 1;

    if (headerUsesCustomDict && !this.options.dictionary) {
      throw new TypeError('Compressed data requires custom dictionary but none provided');
    }
    if (!headerUsesCustomDict && this.options.dictionary) {
      throw new TypeError('Compressed data does not use custom dictionary but one was provided');
    }

    // Use header-derived configuration for window size
    const windowSize = 1 << headerWindow;

    // Validate dictionary size before allocating
    const dictData = headerUsesCustomDict ? new Uint8Array(this.options.dictionary) : null;
    if (dictData && dictData.length !== windowSize) {
      throw new RangeError(`Dictionary size (${dictData.length}) must match header window size (${windowSize})`);
    }

    const { decompressor: decompressorStructSize } = wasmManager.structSizes;

    // Allocate memory for decompressor struct and window buffer in single allocation
    const totalSize = decompressorStructSize + windowSize;
    this.decompressorPtr = this.module._malloc(totalSize);
    if (this.decompressorPtr === 0) {
      throw new RangeError(`Failed to allocate memory for decompressor (${totalSize} bytes)`);
    }

    this.windowPtr = this.decompressorPtr + decompressorStructSize;

    // Initialize dictionary
    if (dictData) {
      this.module.HEAPU8.set(dictData, this.windowPtr);
    } else {
      this.module.ccall('tamp_initialize_dictionary', null, ['number', 'number'], [this.windowPtr, windowSize]);
    }

    // Initialize decompressor with header-derived configuration
    try {
      const result = this.module.ccall(
        'tamp_decompressor_init',
        'number',
        ['number', 'number', 'number', 'number'],
        [this.decompressorPtr, this.confPtr, this.windowPtr, headerWindow]
      );

      throwOnError(result, 'Decompressor initialization');
      this.decompressorInitialized = true;
    } catch (error) {
      this.module._free(this.decompressorPtr); // Frees both struct and window buffer
      this.decompressorPtr = null;
      this.windowPtr = null;
      throw error;
    }

    this.headerRead = true;
    return 1; // Header consumed 1 byte
  }

  /**
   * Decompress data in incremental chunks.
   * @param {Uint8Array} input - Compressed input data
   * @param {Object} [options] - Options object
   * @param {AbortSignal} [options.signal] - AbortSignal for cancellation
   * @returns {Promise<Uint8Array>} - Decompressed output
   */
  async decompress(input, options = {}) {
    await this.initialize();

    if (!this.initialPtr) {
      throw new Error('Decompressor has been destroyed');
    }

    const { signal } = options;

    // Check for cancellation
    const checkAborted = () => {
      if (signal?.aborted) {
        throw new DecompressionError('Decompression was aborted', {
          aborted: true,
          reason: signal.reason,
        });
      }
    };

    checkAborted(); // Check before starting

    // Combine any pending input with new input
    const combinedInput = new Uint8Array(this.pendingInput.length + input.length);
    combinedInput.set(this.pendingInput);
    combinedInput.set(input, this.pendingInput.length);

    let inputOffset = 0;

    // Read header if not already done
    if (!this.headerRead) {
      const headerBytesConsumed = await this._readHeader(combinedInput);
      inputOffset += headerBytesConsumed;

      // If we only have header data, store remaining for next call
      if (inputOffset >= combinedInput.length) {
        this.pendingInput = new Uint8Array(0);
        return new Uint8Array(0);
      }
    }

    // Process remaining data as payload
    const payloadData = combinedInput.slice(inputOffset);

    if (payloadData.length === 0) {
      this.pendingInput = new Uint8Array(0);
      return new Uint8Array(0);
    }

    const CHUNK_SIZE = 1 << 22;
    const outputChunks = [];

    // Single allocation for output size pointer (4 bytes) + input consumed pointer (4 bytes) + input buffer + output buffer
    // Put fixed-size pointers first to avoid alignment issues
    const totalAllocSize = 8 + payloadData.length + CHUNK_SIZE;
    const basePtr = this.module._malloc(totalAllocSize);
    if (basePtr === 0) {
      throw new RangeError(`Failed to allocate decompress memory (${totalAllocSize} bytes)`);
    }

    const outputSizePtr = basePtr;
    const inputConsumedPtr = outputSizePtr + 4;
    const inputPtr = inputConsumedPtr + 4;
    const outputPtr = inputPtr + payloadData.length;

    try {
      this.module.HEAPU8.set(payloadData, inputPtr);

      let payloadOffset = 0;
      let payloadSize = payloadData.length;

      // Process data incrementally
      while (payloadSize > 0) {
        checkAborted(); // Check for cancellation at each iteration
        const result = this.module.ccall(
          'tamp_decompressor_decompress_cb',
          'number',
          ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
          [
            this.decompressorPtr,
            outputPtr,
            CHUNK_SIZE,
            outputSizePtr,
            inputPtr + payloadOffset,
            payloadSize,
            inputConsumedPtr,
            0, // NULL callback
            0, // NULL user_data
          ]
        );

        const outputSize = this.module.getValue(outputSizePtr, 'i32');
        const inputConsumed = this.module.getValue(inputConsumedPtr, 'i32');

        payloadSize -= inputConsumed;
        payloadOffset += inputConsumed;

        if (outputSize > 0) {
          const outputChunk = new Uint8Array(this.module.HEAPU8.buffer, outputPtr, outputSize).slice();
          outputChunks.push(outputChunk);
        }

        if (result === 2) {
          // TAMP_INPUT_EXHAUSTED - store remaining data for next call
          if (payloadSize > 0) {
            this.pendingInput = payloadData.slice(payloadOffset);
          } else {
            this.pendingInput = new Uint8Array(0);
          }
          break;
        } else if (result < 0) {
          throwOnError(result, 'Decompression');
        }

        // If no more input to process, break
        if (payloadSize === 0) {
          this.pendingInput = new Uint8Array(0);
          break;
        }
      }

      if (outputChunks.length === 0) {
        return new Uint8Array(0);
      } else if (outputChunks.length === 1) {
        return outputChunks[0];
      } else {
        const totalSize = outputChunks.reduce((sum, chunk) => sum + chunk.length, 0);
        const result = new Uint8Array(totalSize);
        let offset = 0;
        for (const chunk of outputChunks) {
          result.set(chunk, offset);
          offset += chunk.length;
        }
        return result;
      }
    } finally {
      this.module._free(basePtr);
    }
  }

  /**
   * Clean up allocated memory
   * @returns {void}
   */
  destroy() {
    if (this.decompressorPtr) {
      this.module._free(this.decompressorPtr); // Frees both struct and window buffer
      this.decompressorPtr = null;
      this.windowPtr = null; // Don't free separately - part of same allocation
    }
    if (this.initialPtr) {
      this.module._free(this.initialPtr); // Frees conf, inputConsumed, and headerInput
      this.initialPtr = null;
      this.confPtr = null;
      this.inputConsumedPtr = null;
      this.headerInputPtr = null;
    }
    this.headerRead = false;
    this.decompressorInitialized = false;
    this.pendingInput = new Uint8Array(0);
  }
}

/**
 * One-shot compression function
 * @param {Uint8Array} data - Data to compress
 * @param {import('./tamp.d.ts').TampOptions|Object} [options] - Compression options
 * @param {Function} [options.onPoll] - Progress callback with rich progress info
 * @param {AbortSignal} [options.signal] - AbortSignal for cancellation
 * @param {number} [options.pollIntervalMs=100] - Minimum interval between progress callbacks in milliseconds
 * @param {number} [options.pollIntervalBytes=65536] - Minimum bytes processed between progress callbacks
 * @returns {Promise<Uint8Array>} - Compressed data
 */
export async function compress(data, options = {}) {
  // Separate compression options from callback options
  const compressionOptions = {};
  const callbackOptions = {};

  // Extract compression-specific options
  const { window, literal, dictionary, lazy_matching, onPoll, signal, pollIntervalMs, pollIntervalBytes } = options;
  if (window !== undefined) compressionOptions.window = window;
  if (literal !== undefined) compressionOptions.literal = literal;
  if (dictionary !== undefined) compressionOptions.dictionary = dictionary;
  if (lazy_matching !== undefined) compressionOptions.lazy_matching = lazy_matching;

  // Extract callback options
  callbackOptions.onPoll = onPoll;
  callbackOptions.signal = signal;
  if (pollIntervalMs !== undefined) callbackOptions.pollIntervalMs = pollIntervalMs;
  if (pollIntervalBytes !== undefined) callbackOptions.pollIntervalBytes = pollIntervalBytes;

  const compressor = new TampCompressor(compressionOptions);
  try {
    const compressed = await compressor.compress(data, callbackOptions);
    const flushed = await compressor.flush();

    // Concatenate compressed data and flush output
    const result = new Uint8Array(compressed.length + flushed.length);
    result.set(compressed);
    result.set(flushed, compressed.length);

    return result;
  } finally {
    compressor.destroy();
  }
}

/**
 * One-shot decompression function
 * @param {Uint8Array} data - Compressed data to decompress
 * @param {import('./tamp.d.ts').TampOptions} [options] - Decompression options
 * @returns {Promise<Uint8Array>} - Decompressed data
 */
export async function decompress(data, options = {}) {
  const decompressor = new TampDecompressor(options);
  try {
    return await decompressor.decompress(data);
  } finally {
    decompressor.destroy();
  }
}

/**
 * Initialize the WebAssembly module
 * @returns {Promise<void>}
 */
export async function initialize() {
  await initializeWasm();
}

/**
 * Utility function to initialize a dictionary buffer with default values
 * @param {number} size - Size of the dictionary buffer (must be power of 2)
 * @returns {Uint8Array} - Initialized dictionary buffer
 */
export async function initializeDictionary(size) {
  if (!Number.isInteger(size) || size <= 0 || (size & (size - 1)) !== 0) {
    throw new RangeError('Dictionary size must be a positive power of 2');
  }

  const module = await initializeWasm();
  const bufferPtr = module._malloc(size);
  if (bufferPtr === 0) {
    throw new RangeError(`Failed to allocate dictionary buffer (${size} bytes)`);
  }

  try {
    // Call the C function to initialize the dictionary
    module.ccall('tamp_initialize_dictionary', null, ['number', 'number'], [bufferPtr, size]);

    const dictionary = new Uint8Array(module.HEAPU8.buffer, bufferPtr, size).slice();
    return dictionary;
  } finally {
    module._free(bufferPtr);
  }
}

/**
 * Compute the minimum pattern size for given window and literal parameters
 * @param {number} window - Number of window bits (8-15)
 * @param {number} literal - Number of literal bits (5-8)
 * @returns {number} - Minimum pattern size in bytes (2 or 3)
 */
export async function computeMinPatternSize(window, literal) {
  if (!Number.isInteger(window) || window < 8 || window > 15) {
    throw new RangeError('Window must be an integer between 8 and 15');
  }
  if (!Number.isInteger(literal) || literal < 5 || literal > 8) {
    throw new RangeError('Literal must be an integer between 5 and 8');
  }

  const module = await initializeWasm();
  return module.ccall('tamp_compute_min_pattern_size', 'number', ['number', 'number'], [window, literal]);
}

/**
 * Compress text string to bytes
 * @param {string} text - Text string to compress
 * @param {import('./tamp.d.ts').TampOptions|Object} [options] - Compression options
 * @param {Function} [options.onPoll] - Progress callback with rich progress info
 * @param {AbortSignal} [options.signal] - AbortSignal for cancellation
 * @param {number} [options.pollIntervalMs=100] - Minimum interval between progress callbacks in milliseconds
 * @param {number} [options.pollIntervalBytes=65536] - Minimum bytes processed between progress callbacks
 * @returns {Promise<Uint8Array>} - Compressed data
 */
export async function compressText(text, options = {}) {
  const data = new TextEncoder().encode(text);
  return await compress(data, options);
}

/**
 * Decompress bytes to text string
 * @param {Uint8Array} data - Compressed data to decompress
 * @param {import('./tamp.d.ts').TampOptions} [options] - Decompression options
 * @param {string} [encoding='utf-8'] - Text encoding to use
 * @returns {Promise<string>} - Decompressed text
 */
export async function decompressText(data, options = {}, encoding = 'utf-8') {
  const decompressed = await decompress(data, options);
  return new TextDecoder(encoding).decode(decompressed);
}

/**
 * Automatic resource management helper
 * @param {TampCompressor|TampDecompressor} resource - Resource to manage
 * @param {(resource: TampCompressor|TampDecompressor) => Promise<any>|any} fn - Function to execute with the resource
 * @returns {Promise<any>} - Result of the function
 */
export async function using(resource, fn) {
  try {
    return await fn(resource);
  } finally {
    if (resource && typeof resource.destroy === 'function') {
      resource.destroy();
    }
  }
}

export { TampError, ExcessBitsError, CompressionError, DecompressionError };
