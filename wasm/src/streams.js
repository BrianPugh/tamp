/**
 * Web Streams API implementation for Tamp compression/decompression
 */

import { TampCompressor, TampDecompressor } from './tamp.js';

/**
 * Transform stream for compression using Web Streams API
 */
export class TampCompressionStream extends TransformStream {
  constructor(options = {}) {
    let compressor = null;

    super({
      start(_controller) {
        compressor = new TampCompressor(options);
      },

      async transform(chunk, controller) {
        try {
          // Ensure chunk is Uint8Array
          const input = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);

          const compressed = await compressor.compress(input);
          if (compressed.length > 0) {
            controller.enqueue(compressed);
          }
        } catch (error) {
          controller.error(error);
        }
      },

      async flush(controller) {
        try {
          const finalData = await compressor.flush();
          if (finalData.length > 0) {
            controller.enqueue(finalData);
          }
        } catch (error) {
          controller.error(error);
        } finally {
          if (compressor) {
            compressor.destroy();
          }
        }
      },
    });
  }
}

/**
 * Transform stream for decompression using Web Streams API
 */
export class TampDecompressionStream extends TransformStream {
  constructor(options = {}) {
    let decompressor = null;

    super({
      start(_controller) {
        decompressor = new TampDecompressor(options);
      },

      async transform(chunk, controller) {
        try {
          // Ensure chunk is Uint8Array
          const input = chunk instanceof Uint8Array ? chunk : new Uint8Array(chunk);

          // Decompress chunk directly - decompressor handles its own buffering
          const decompressed = await decompressor.decompress(input);
          if (decompressed.length > 0) {
            controller.enqueue(decompressed);
          }
        } catch (error) {
          controller.error(error);
        }
      },

      async flush(controller) {
        try {
          // Decompressor handles its own pending data internally
          // Just try to process any remaining data with an empty chunk
          const decompressed = await decompressor.decompress(new Uint8Array(0));
          if (decompressed.length > 0) {
            controller.enqueue(decompressed);
          }
        } catch (error) {
          controller.error(error);
        } finally {
          if (decompressor) {
            decompressor.destroy();
          }
        }
      },
    });
  }
}

/**
 * Convenience function to compress a stream
 * @param {ReadableStream} readable - Input stream
 * @param {TampOptions} options - Compression options
 * @returns {ReadableStream} - Compressed output stream
 */
export function compressStream(readable, options = {}) {
  return readable.pipeThrough(new TampCompressionStream(options));
}

/**
 * Convenience function to decompress a stream
 * @param {ReadableStream} readable - Compressed input stream
 * @param {TampOptions} options - Decompression options
 * @returns {ReadableStream} - Decompressed output stream
 */
export function decompressStream(readable, options = {}) {
  return readable.pipeThrough(new TampDecompressionStream(options));
}

/**
 * Helper function to convert a Uint8Array to a ReadableStream
 * @param {Uint8Array} data - Data to convert
 * @param {number} chunkSize - Size of each chunk (default: 8192)
 * @returns {ReadableStream}
 */
export function createReadableStream(data, chunkSize = 8192) {
  let offset = 0;

  return new ReadableStream({
    pull(controller) {
      if (offset >= data.length) {
        controller.close();
        return;
      }

      const chunk = data.slice(offset, offset + chunkSize);
      offset += chunkSize;
      controller.enqueue(chunk);
    },
  });
}

/**
 * Helper function to collect a ReadableStream into a Uint8Array
 * @param {ReadableStream} readable - Stream to collect
 * @returns {Promise<Uint8Array>} - Collected data
 */
export async function collectStream(readable) {
  const chunks = [];
  const reader = readable.getReader();

  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
    }
  } finally {
    reader.releaseLock();
  }

  // Calculate total length and create result array
  const totalLength = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
  const result = new Uint8Array(totalLength);

  let offset = 0;
  for (const chunk of chunks) {
    result.set(chunk, offset);
    offset += chunk.length;
  }

  return result;
}
