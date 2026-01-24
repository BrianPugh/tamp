/**
 * TypeScript type definitions for Tamp WebAssembly Streams
 */

import type { TampOptions } from './tamp';

export declare class TampCompressionStream extends TransformStream<Uint8Array, Uint8Array> {
  constructor(options?: TampOptions);
}

export declare class TampDecompressionStream extends TransformStream<Uint8Array, Uint8Array> {
  constructor(options?: TampOptions);
}

export declare function compressStream(
  readable: ReadableStream<Uint8Array>,
  options?: TampOptions
): ReadableStream<Uint8Array>;

export declare function decompressStream(
  readable: ReadableStream<Uint8Array>,
  options?: TampOptions
): ReadableStream<Uint8Array>;

export declare function createReadableStream(data: Uint8Array, chunkSize?: number): ReadableStream<Uint8Array>;

export declare function collectStream(readable: ReadableStream<Uint8Array>): Promise<Uint8Array>;
