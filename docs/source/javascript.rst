JavaScript/TypeScript API
=========================

The Tamp WebAssembly package provides JavaScript and TypeScript bindings for the Tamp compression library.

Installation
------------

.. code-block:: bash

   npm install @brianpugh/tamp

Basic Usage
-----------

Simple Compression
^^^^^^^^^^^^^^^^^^

Compress/decompress in-memory in a single operation:

.. code-block:: javascript

   import { compress, decompress, compressText, decompressText } from '@brianpugh/tamp';

   // Compress binary data
   const originalData = new TextEncoder().encode('Hello, World!');
   const compressed = await compress(originalData);
   const decompressed = await decompress(compressed);

   // Compress text directly
   const compressedText = await compressText('Hello, World!');
   const restoredText = await decompressText(compressedText);

Configuration Options
^^^^^^^^^^^^^^^^^^^^^

Customize compression behavior with options:

.. code-block:: javascript

   const options = {
     // Describes the size of the decompression buffer in bits.
     // A 10-bit window represents a 1024-byte buffer.
     // Must be in range [8, 15], representing [256, 32768] byte windows.
     window: 12,

     // Number of bits occupied in each plaintext symbol.
     // For example, if ASCII text is being encoded, then we could set this
     // value to 7 to slightly improve compression ratios.
     // Must be in range [5, 8].
     // For general use, 8 (the whole byte) is appropriate.
     literal: 7,

     // Enable lazy matching to slightly improve compression (0.5-2.0%) ratios
     // at the cost of 50-75% slower compression.
     // Most embedded systems will **not** want to use this feature and disable it.
     lazy_matching: true,

     // To improve compression ratios for very short messages, a custom
     // buffer initialization could be used.
     // For most use-cases, set this to null.
     dictionary: null
   };

   const compressed = await compress(data, options);
   const decompressed = await decompress(compressed, options);


Streaming for Large Data
^^^^^^^^^^^^^^^^^^^^^^^^^

Use streaming compression for processing large files or data that does not fit in memory:

.. code-block:: javascript

   import { TampCompressor, TampDecompressor, using } from '@brianpugh/tamp';

   // Automatic resource management (recommended)
   const compressedChunks = [];
   await using(new TampCompressor(options), async (compressor) => {
     for (const chunk of dataChunks) {
       const compressedChunk = await compressor.compress(chunk);
       if (compressedChunk.length > 0) {
         compressedChunks.push(compressedChunk);
       }
     }
     // Flush remaining data
     const finalChunk = await compressor.flush();
     if (finalChunk.length > 0) {
       compressedChunks.push(finalChunk);
     }
   });


Web Streams Integration
^^^^^^^^^^^^^^^^^^^^^^^

Use with the Web Streams API for seamless integration with modern web applications:

.. code-block:: javascript

   import { TampCompressionStream, TampDecompressionStream } from '@brianpugh/tamp/streams';

   // Compress a file stream
   const fileStream = file.stream();
   const compressionStream = new TampCompressionStream(options);
   const compressedStream = fileStream.pipeThrough(compressionStream);

   // Save compressed data
   const response = new Response(compressedStream);
   const compressedBlob = await response.blob();

   // Or chain compression and decompression
   const decompressionStream = new TampDecompressionStream(options);
   const roundTripStream = fileStream
     .pipeThrough(compressionStream)
     .pipeThrough(decompressionStream);


Error Handling
--------------

The library throws specific error types:

- ``TampError`` - Base error class
- ``CompressionError`` - Compression operation errors
- ``DecompressionError`` - Decompression operation errors
- ``ExcessBitsError`` - Data exceeds literal size limits

Custom Configuration
--------------------
Configure compression parameters by passing in options:

.. code-block:: javascript

   const options = {
     window: 12,           // Larger window for (usually) better compression
     literal: 7,           // ASCII text only requires 7 bits.
     lazy_matching: true   // Better compression ratios; slower to compress
   };

   const compressed = await compress(data, options);
   const decompressed = await decompress(compressed, options);

Progress Callbacks
------------------

Monitor compression progress for large files using progress callbacks. Progress callbacks receive a ``progressInfo`` object with detailed compression metrics:

.. code-block:: javascript

   // Basic progress monitoring
   const progressCallback = (progressInfo) => {
     console.log(`${progressInfo.percent.toFixed(1)}% complete`);
     console.log(`Speed: ${(progressInfo.bytesPerSecond / 1024).toFixed(1)} KB/s`);
     console.log(`ETA: ${progressInfo.estimatedTimeRemaining.toFixed(1)}s`);
   };

   const compressed = await compress(largeData, options, progressCallback);

   // Conditional abortion example
   const abortingCallback = (progressInfo) => {
     console.log(`Progress: ${progressInfo.percent.toFixed(1)}%`);

     // Throw error to abort compression after 50%
     if (progressInfo.percent > 50) {
       throw new Error('Compression cancelled by user');
     }
   };

   try {
     const compressed = await compress(data, options, abortingCallback);
   } catch (error) {
     console.log('Compression was aborted by callback');
   }

**Progress Info Object Fields:**

.. code-block:: typescript

   interface ProgressInfo {
     bytesProcessed: number;        // Number of input bytes processed so far
     totalBytes: number;            // Total number of input bytes
     percent: number;               // Completion percentage (0-100)
     bytesPerSecond: number;        // Processing speed in bytes/second
     estimatedTimeRemaining: number; // Estimated seconds remaining
     chunksProcessed: number;       // Number of chunks processed
     elapsedTime: number;           // Total elapsed time in seconds
     chunkSize: number;             // Size of current chunk being processed
     outputSize: number;            // Size of output written for current chunk
   }

The decompressor does not have a progress callback since the operation is usually very very fast.

Utility Functions
-----------------

WebAssembly Initialization
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The WebAssembly module initializes automatically, but you can preload it explicitly:

.. code-block:: javascript

   import { initialize } from '@brianpugh/tamp';

   // Preload WebAssembly module (optional)
   await initialize();

Dictionary Utilities
^^^^^^^^^^^^^^^^^^^^

Create and manage custom dictionaries for improved compression:

.. code-block:: javascript

   import { initializeDictionary, computeMinPatternSize } from '@brianpugh/tamp';

   // Initialize a dictionary buffer with default values
   const dictionary = await initializeDictionary(1024); // Must be power of 2

   // Compute minimum pattern size for given parameters
   const minPatternSize = await computeMinPatternSize(12, 8); // window=12, literal=8

Stream Helpers
^^^^^^^^^^^^^^

Additional utilities for working with streams:

.. code-block:: javascript

   import {
     compressStream,
     decompressStream,
     createReadableStream,
     collectStream
   } from '@brianpugh/tamp';

   // Convert Uint8Array to ReadableStream
   const data = new TextEncoder().encode('Hello, World!');
   const readableStream = createReadableStream(data, 1024); // 1024 byte chunks

   // Compress a readable stream
   const compressedStream = compressStream(readableStream, options);

   // Decompress a readable stream
   const decompressedStream = decompressStream(compressedStream, options);

   // Collect stream back to Uint8Array
   const result = await collectStream(decompressedStream);

Node.js and Browser Support
---------------------------

The package supports both Node.js (>= 14.0.0) and modern browsers with WebAssembly support. It provides CommonJS and ES Module builds.

.. code-block:: javascript

   // ES Modules
   import { compress } from '@brianpugh/tamp';

   // CommonJS
   const { compress } = require('@brianpugh/tamp');
