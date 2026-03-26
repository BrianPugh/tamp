.. _C Library:

C Library
=========
Tamp provides a C library optimized for low-memory-usage, fast runtime, and small binary footprint.
This page describes how to use the provided library.

Compile-Time Flags
^^^^^^^^^^^^^^^^^^
Tamp's C library can be customized via compile-time flags to control features, code size, and performance.
Pass these flags to your compiler (e.g., ``-DTAMP_STREAM=0``).

.. list-table::
   :header-rows: 1
   :widths: 30 15 55

   * - Flag
     - Default
     - Description
   * - TAMP_COMPRESSOR
     - 1
     - Include compressor implementation. Set to ``0`` to exclude compressor code
       entirely, reducing binary size for decompressor-only builds.
   * - TAMP_DECOMPRESSOR
     - 1
     - Include decompressor implementation. Set to ``0`` to exclude decompressor
       code entirely, reducing binary size for compressor-only builds.
   * - TAMP_ESP32
     - 0
     - Use ESP32-optimized variant. Avoids bitfields for speed at the cost of
       slightly higher memory usage. Automatically enabled via Kconfig on ESP-IDF.
   * - TAMP_EXTENDED
     - 1
     - Default value for extended format support (RLE, extended match encoding).
       Set to ``0`` to disable extended support in both compressor and decompressor.
   * - TAMP_EXTENDED_COMPRESS
     - TAMP_EXTENDED
     - Enable extended format compression. Defaults to TAMP_EXTENDED but can
       be individually overridden for compressor-only or decompressor-only builds.
   * - TAMP_EXTENDED_DECOMPRESS
     - TAMP_EXTENDED
     - Enable extended format decompression. Defaults to TAMP_EXTENDED but can
       be individually overridden for compressor-only or decompressor-only builds.
   * - TAMP_LAZY_MATCHING
     - 0
     - Enable lazy matching support. When enabled, ``TampConf.lazy_matching``
       becomes available. Improves compression ratio by 0.5-2% at the cost of
       50-75% slower compression. Most embedded systems should leave disabled.
   * - TAMP_STREAM
     - 1
     - Include stream API (``tamp_compress_stream``, ``tamp_decompress_stream``).
       Most users don't need to change this; with ``-ffunction-sections`` and
       ``--gc-sections`` (standard on embedded toolchains), unused stream functions
       are automatically stripped. Set to ``0`` to guarantee exclusion.
   * - TAMP_STREAM_FATFS
     - 0
     - Enable FatFs (ChaN's FAT filesystem) stream handlers. Requires FatFs headers.
   * - TAMP_STREAM_LITTLEFS
     - 0
     - Enable LittleFS stream handlers. Requires LittleFS headers.
   * - TAMP_STREAM_MEMORY
     - 0
     - Enable memory buffer stream handlers (``TampMemReader``, ``TampMemWriter``).
       Useful for file-to-memory or memory-to-file operations.
   * - TAMP_STREAM_STDIO
     - 0
     - Enable stdio (``FILE*``) stream handlers. Works with standard C library,
       ESP-IDF VFS, and POSIX-compatible systems.
   * - TAMP_STREAM_WORK_BUFFER_SIZE
     - 32
     - Stack-allocated work buffer size (bytes) for stream API. Split in half
       for input/output. Larger values reduce I/O callback invocations,
       improving decompression speed. 256+ bytes recommended when stack permits.
   * - TAMP_USE_MEMSET
     - 1
     - Use libc ``memset``. Set to ``0`` for environments without libc
       (e.g. MicroPython native modules). When disabled, uses a volatile byte loop
       that avoids emitting a ``memset`` call at the cost of store coalescing.

**Example: Minimal decompressor-only build**

.. code-block:: bash

   gcc -DTAMP_COMPRESSOR=0 -DTAMP_STREAM=0 -c decompressor.c common.c

**Example: Full-featured build with LittleFS support**

.. code-block:: bash

   gcc -DTAMP_LAZY_MATCHING=1 -DTAMP_STREAM_LITTLEFS=1 -DTAMP_STREAM_WORK_BUFFER_SIZE=256 \
       -Ipath/to/littlefs -c compressor.c decompressor.c common.c

Overview
^^^^^^^^
To use Tamp in your C project, simply copy the contents of ``tamp/_c_src`` into your project.
The contents are broken down as follows (header files described, but you'll also need the c files):

1. ``tamp/common.h`` - Common functionality needed by both the compressor and decompressor. Must be included.

2. ``tamp/compressor.h`` - Functions to compress a data stream.

3. ``tamp/decompressor.h`` - Functions to decompress a data stream.

All header files are well documented.
Please refer to the appropriate header file for precise API usage.
This document primarily serves as suggestions on how to use the library, and some of the philosophy behind it.

Compressor
^^^^^^^^^^
To include the compressor functionality, include the compressor header:

.. code-block:: c

   #include "tamp/compressor.h"

Initialization
--------------
All compression is performed using a ``TampCompressor`` object.
The object must first be initialized with ``tamp_compressor_init``.
The compressor object, a configuration, and a buffer are provided.
Tamp performs no internal memory allocations, so a buffer must be provided.
Tamp is an LZ-based compression schema, and this buffer is commonly called a "window buffer" or "dictionary".
The provided buffer must be at least ``(1 << conf.window)`` bytes.


.. code-block:: c

   static unsigned char window_buffer[1024];

   TampConf conf = {
      /* Describes the size of the buffer in bits.
      A 10-bit window represents a 1024-byte buffer.
      Must be in range [8, 15], representing [256, 32768] byte windows.
      10 is a good default, balancing compression ratio, memory, and speed. */
      .window = 10,

      /* Number of bits occupied in each plaintext symbol.
      For example, if ASCII text is being encoded, then we could set this
      value to 7 to slightly improve compression ratios.
      Must be in range [5, 8].
      For general use, 8 (the whole byte) is appropriate. */
      .literal = 8,

      /* To improve compression ratios for very short messages, a custom
      buffer initialization could be used.
      For most use-cases, set this to false.*/
      .use_custom_dictionary = false,

      /* Enable lazy matching to slightly improve compression (0.5-2.0%) ratios
      at the cost of 50-75% slower compression.
      Not recommended for most embedded applications.
      Only available when compiled with -DTAMP_LAZY_MATCHING=1. */
      .lazy_matching = false,

      /* Enable extended format (RLE and extended match encoding) for improved
      compression ratios. Default is true in v2. The only downside is
      slightly larger firmware; compression and decompression speed are
      unaffected.
      Only available when compiled with -DTAMP_EXTENDED_COMPRESS=1 (default). */
      .extended = true,

      /* Enable dictionary reset / append support (v2.1.0+).
      Adds a second header byte; old decompressors reject the stream.
      Required for reset_dictionary() and append mode. */
      .dictionary_reset = false,
   };
   TampCompressor compressor;
   tamp_compressor_init(&compressor, &conf, window_buffer);


Compression
-----------
Once the ``TampCompressor`` object is initialized, compression of data can be performed.
The compressor is most efficient when provided with larger input buffers, as more context allows better pattern matching.
There are two API levels: a higher-level API for most use cases, and a low-level API for fine-grained control.

The higher-level functions handle the internal sink/poll loop:

* ``tamp_compressor_compress`` - Compresses input until the input is exhausted or the output buffer is full.
* ``tamp_compressor_compress_and_flush`` - Same as above, but also flushes internal buffers at the end (see `Flushing`_).

For most use cases, ``tamp_compressor_compress_and_flush`` is the right choice.

Low-Level API
~~~~~~~~~~~~~
The low-level API gives direct control over compression timing, which can be useful for real-time systems:

1. ``tamp_compressor_sink`` - Copy bytes into the compressor's internal input buffer (up to 16 bytes). This is computationally cheap.

2. ``tamp_compressor_poll`` - Perform a single compression cycle on the internal buffer, consuming up to 15 bytes (or all 16 with extended format). This is the computationally intensive step, writing 0-3 bytes to the output (up to 5 with extended format, requiring 6 bytes of output buffer space). Compression is most efficient when the internal input buffer is full.

Breaking the operation into two functions allows ``tamp_compressor_poll`` to be called at a more opportune time in your program.

.. code-block:: c

    while(input_size > 0 && output_size > 0){
        {
            // Sink Data into input buffer.
            size_t consumed;
            tamp_compressor_sink(compressor, input, input_size, &consumed);
            input += consumed;
            input_size -= consumed;
        }
        {
            // Perform 1 compression cycle on internal input buffer
            size_t chunk_output_written_size;
            res = tamp_compressor_poll(compressor, output, output_size, &chunk_output_written_size);
            output += chunk_output_written_size;
            output_size -= chunk_output_written_size;
            assert(res == TAMP_OK);
        }
    }

Flushing
--------
Inside the compressor, there may be up to 16 **bytes** of uncompressed data in the input buffer, and 31 **bits** in the output buffer.
This means that the compressed output lags behind the input data stream.

For example, if we compress the 44-long non-null-terminated string ``"The quick brown fox jumped over the lazy dog"``,
the compressor will produce a 32-long data stream, that decompresses to ``"The quick brown fox jumped ov"``.
The remaining ``"er the lazy dog"`` is still in the compressor's internal buffers.

To flush the remaining data, use ``tamp_compressor_flush``, which drains the internal input buffer via repeated ``tamp_compressor_poll`` calls, then flushes the output bit buffer.

.. code-block:: c

   tamp_res res;
   unsigned char output_buffer[100];
   size_t output_written;  // Stores the resulting number of bytes written to output_buffer.

   res = tamp_compressor_flush(&compressor, output_buffer, sizeof(output_buffer), &output_written, true);
   assert(res == TAMP_OK);

The ``write_token`` parameter controls whether a ``FLUSH`` token is appended when padding is required:

* Set to ``true`` if you intend to continue using the compressor. The ``FLUSH`` token adds 0~2 bytes of overhead.
* Set to ``false`` when finalizing a stream, to save 0~2 bytes.

When ``dictionary_reset`` is enabled, ``write_token=true`` **always** emits a
FLUSH token (even when byte-aligned), and closing the stream emits one
automatically. This enables the append mode described below.

Dictionary Reset
----------------

.. note::

   New in v2.1.0. Requires v2.1.0+ compressor and decompressor.

Dictionary reset allows appending new compressed data to an existing stream
without retaining the previous compressor state. After a reset, both compressor
and decompressor start fresh with a default dictionary, so no prior window
buffer, input buffer, or bit buffer state needs to be persisted.

``tamp_compressor_reset_dictionary`` writes a double-FLUSH signal, resets the
dictionary and all internal state, then continues with the same configuration.
The decompressor handles double-FLUSH resets automatically.

Requires ``.dictionary_reset = true`` in ``TampConf``, which adds a second
header byte. Older decompressors reject the stream at the header level.
Returns ``TAMP_INVALID_CONF`` if not set.

.. code-block:: c

   TampConf conf = {.window = 10, .literal = 8, .dictionary_reset = true};
   TampCompressor compressor;
   tamp_compressor_init(&compressor, &conf, window_buffer);

   // ... compress some data ...

   unsigned char reset_buffer[32];  // Worst case: 27 bytes
   size_t reset_written;
   res = tamp_compressor_reset_dictionary(&compressor, reset_buffer,
                                          sizeof(reset_buffer), &reset_written);
   // Append reset_buffer[0..reset_written] to output, then continue compressing.

Append Mode
~~~~~~~~~~~
Set ``.append = true`` to resume compression on an existing stream without
persisting compressor state. Requires:

1. The previous compression stream to have had ``dictionary_reset = true``.

2. The new compressor to have the same configuration as the previous compressor (same ``window_bits``, ``literal``, etc).

Writes a byte-aligned
FLUSH token to the internal bit buffer instead of a header. Combined with the
previous stream's trailing FLUSH, this forms a double-FLUSH that resets the
decompressor's dictionary.

Requires ``dictionary_reset = true`` and ``use_custom_dictionary = false``.

.. code-block:: c

   // Session 1
   TampConf conf = {.window = 10, .literal = 8, .dictionary_reset = true};
   TampCompressor compressor;
   tamp_compressor_init(&compressor, &conf, window_buffer);
   tamp_compressor_compress_and_flush(&compressor, output, output_size,
                                      &output_written, input, input_size,
                                      &input_consumed, true);
   // Save output to storage (stream ends with FLUSH token).

   // Session 2
   conf.append = true;
   TampCompressor compressor2;
   tamp_compressor_init(&compressor2, &conf, window_buffer);
   tamp_compressor_compress_and_flush(&compressor2, output, output_size,
                                      &output_written, new_input, new_input_size,
                                      &input_consumed, true);
   // Append output to storage.

Lazy Matching
-------------
Lazy matching is a compression optimization that can slightly improve compression ratios at the cost of slower compression speed and increased firmware size.
To compile the library with lazy matching support, define the ``TAMP_LAZY_MATCHING`` macro to ``1`` during compilation:

.. code-block:: c

   #define TAMP_LAZY_MATCHING 1
   #include "tamp/compressor.h"

Alternatively, use the compiler flag ``-DTAMP_LAZY_MATCHING=1``.

When compiled with ``TAMP_LAZY_MATCHING=1``, the ``TampConf.lazy_matching`` field becomes available and can be set to enable this feature for individual compressor instances.

Minimizing Output Buffer Size
-----------------------------

The following table shows the minimum output buffer size required for each operation.
``tamp_compressor_flush`` calls ``tamp_compressor_poll`` until the internal input buffer is exhausted; the flush rows reflect the final output after exhaustion.

+--------------------------------------+----------+----------+
| Operation                            | Standard | Extended |
+======================================+==========+==========+
| ``poll``                             | 3 bytes  | 6 bytes  |
+--------------------------------------+----------+----------+
| ``flush`` (``write_token=false``)    | 4 bytes  | 6 bytes  |
+--------------------------------------+----------+----------+
| ``flush`` (``write_token=true``)     | 5 bytes  | 7 bytes  |
+--------------------------------------+----------+----------+

If output buffer sizing is a concern, consider using the `Stream API`_ instead.
The stream API handles chunked I/O internally using a small stack-allocated work buffer, eliminating the need to size output buffers manually:

.. code-block:: c

    // Compile with: -DTAMP_STREAM_STDIO=1

    FILE *in = fopen("input.txt", "rb");
    FILE *out = fopen("output.tamp", "wb");
    unsigned char window[1 << 10];

    TampCompressor compressor;
    tamp_compressor_init(&compressor, NULL, window);

    tamp_compress_stream(
        &compressor,
        tamp_stream_stdio_read, in,
        tamp_stream_stdio_write, out,
        NULL, NULL, NULL, NULL
    );

    fclose(in);
    fclose(out);

For users of the buffer-based API, the maximum number of bytes (and thus, the suggested output buffer size) that can be flushed from a compressor with a full internal input buffer via ``tamp_compressor_flush`` can be calculated as:

.. math::

   max\_output\_size = \left\lceil\frac{16 + \text{window} + 16(1 + \text{literal})}{8}\right\rceil

The math for with ``write_token=true`` is more complicated, but it just so happens that in all valid configuration cases, it requires 1 more byte in the output buffer:

+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| Literal Size (Bits) | Window Size (Bits) | Max Output Size write_token=false (Bytes) | Max Output Size write_token=true (Bytes) |
+=====================+====================+===========================================+==========================================+
| 5                   | 8                  | 15                                        | 16                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 5                   | 9-15               | 16                                        | 17                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 6                   | 8                  | 17                                        | 18                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 6                   | 9-15               | 18                                        | 19                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 7                   | 8                  | 19                                        | 20                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 7                   | 9-15               | 20                                        | 21                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 8                   | 8                  | 21                                        | 22                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+
| 8                   | 9-15               | 22                                        | 23                                       |
+---------------------+--------------------+-------------------------------------------+------------------------------------------+

For most applications, ``literal=8`` and ``window=10`` offers a good tradeoff, and should have an output buffer size of at least 22 bytes.

Summary
-------

.. code-block:: c

   unsigned char window_buffer[1024];
   const unsigned char input_string[44] = "The quick brown fox jumped over the lazy dog";
   unsigned char output_buffer[64];

   TampConf conf = {.window=10, .literal=8};
   TampCompressor compressor;
   tamp_compressor_init(&compressor, &conf, window_buffer);

   size_t input_consumed_size, output_written_size;
   tamp_compressor_compress_and_flush(
        &compressor,
        output_buffer, sizeof(output_buffer), &output_written_size,
        input_string, sizeof(input_string), &input_consumed_size,
        false  // Don't write flush token
   );

   // Compressed data is now in output_buffer
   printf("Compressed size: %zu\n", output_written_size);

Decompressor
^^^^^^^^^^^^
The decompressor API is much simpler than the compressor API.
To include the decompressor functionality, include the decompressor header:

.. code-block:: c

   #include "tamp/decompressor.h"

Initialization
--------------
All decompression is performed using a ``TampDecompressor`` object.
Like ``TampCompressor``, this object needs to be configured with a ``TampConf`` object.
Typically, this configuration comes from the Tamp header at the beginning of the compressed data.
Use ``tamp_decompressor_read_header`` to read the header into a ``TampConf``:

.. code-block:: c

   const unsigned char *compressed_data = ...;  // Imagine this points to tamp-compressed data.
   size_t compressed_data_size = 64;
   tamp_res res;
   TampConf conf;
   size_t compressed_consumed_size;

   // This will populate conf.
   res = tamp_decompressor_read_header(
       &conf,
       compressed_data, compressed_data_size, &compressed_consumed_size
   );
   assert(res == TAMP_OK);

   compressed_data += compressed_consumed_size;
   compressed_data_size -= compressed_consumed_size;

   // TODO: actual decompression.

Explicitly reading the header is useful if the window-buffer needs to be dynamically allocated.
The window-buffer size can be calculated as ``(1 << conf.window)``.
If a static window buffer is used, then ``tamp_decompressor_read_header`` doesn't need to be explicitly called.
``tamp_decompressor_init`` initializes the actual decompressor object, using an optionally supplied ``TampConf``.
If no ``TampConf`` is provided, then it will be automatically initialized on first ``tamp_decompressor_decompress``
call from input header data.

.. code-block:: c

   TampDecompressor decompressor;
   unsigned char window_buffer[1024];
   tamp_res res;

   // Since no TampConf is provided, the header will automatically be parsed
   // in the first tamp_decompressor_decompress call.
   // The last parameter (10) indicates the buffer can accommodate up to a 10-bit window.
   res = tamp_decompressor_init(&decompressor, NULL, window_buffer, 10);

   assert(res == TAMP_OK);

Decompression
-------------
Data decompression is straightforward:

.. code-block:: c

   const unsigned char input_data[64]; // Hypothetical input compressed data.
   size_t input_consumed_size;

   unsigned char output_data[64];  // output decompressed data
   size_t output_written_size;

   res = tamp_decompressor_decompress(
       &decompressor,
       output_data, sizeof(output_data), &output_written_size,
       input_data, sizeof(input_data), &input_consumed_size
   );
   // res could be:
   //    TAMP_INPUT_EXHAUSTED - All data in input buffer has been consumed.
   //    TAMP_OUTPUT_FULL - Output buffer is full.
   // In all situations, output_written_size and input_consumed_size are updated.

Callbacks
^^^^^^^^^
``tamp_compressor_compress``, ``tamp_compressor_compress_and_flush``, and ``tamp_decompressor_decompress`` each have callback variants (``_cb`` suffix) that accept two additional arguments:

* ``callback``, a function with signature:

  .. code-block:: c

      int callback(void *user_data, size_t bytes_processed, size_t total_bytes);

  Where ``bytes_processed`` is the number of input bytes consumed so far, and
  ``total_bytes`` is the ``input_size`` passed to this call.
  This allows computing a progress percentage as ``bytes_processed / total_bytes``.
  Return 0 to continue, or non-zero to abort the operation.
  The return value is truncated to ``tamp_res`` (``int8_t``); use values in [100, 127] or [-128, -100] for custom codes.

* ``void *user_data``, arbitrary data passed along to the callback.

Callbacks are useful for resetting a watchdog, updating a progress bar, etc.

The callback fires once per compression or decompression cycle (i.e., once per encoded or decoded token).
Note that ``tamp_compressor_compress_cb`` only fires the callback when the internal input buffer is full, so trailing input bytes that don't fill the buffer won't trigger a callback.
``tamp_compressor_compress_and_flush_cb`` addresses this with a final callback after flushing.
The `Stream API`_ also accepts an optional progress callback; it fires once per read-chunk and can abort the stream in the same way (non-zero return).

Stream API
^^^^^^^^^^
Tamp provides a high-level stream API for compressing/decompressing between files, memory buffers, or **any custom source/sink**.
Tamp is a one-shot compression algorithm: it reads input sequentially and streams output without seeking, making it suitable for non-seekable sources like UART or network sockets.
The stream API uses **callbacks** for reading and writing, making it filesystem-agnostic.
Built-in handlers are provided for common use cases.

Callback Types
--------------
The stream API uses two callback types defined in ``common.h``:

.. code-block:: c

   // Read callback: read up to `size` bytes into `buffer`
   // Returns bytes read (0 for EOF), or negative on error
   typedef int (*tamp_read_t)(void *handle, unsigned char *buffer, size_t size);

   // Write callback: write `size` bytes from `buffer`
   // Returns bytes written, or negative on error
   typedef int (*tamp_write_t)(void *handle, const unsigned char *buffer, size_t size);

A negative return from the read callback is promoted to ``TAMP_READ_ERROR``.
A return of 0 indicates EOF.

A negative return or incomplete write from the write callback is promoted to ``TAMP_WRITE_ERROR``.
Chunks are small (see `Work Buffer`_), so writing the full amount is expected.

Work Buffer
-----------
The stream functions use an internal **stack-allocated** work buffer for intermediate I/O operations.
This buffer is split in half internally: one half for reading input, the other for writing output.
Larger buffers reduce the number of I/O callback invocations.
Compression is generally CPU bound and is unaffected by the work buffer size.
Decompression is more IO bound and benefits from a larger work buffer.

The buffer size is controlled by the ``TAMP_STREAM_WORK_BUFFER_SIZE`` macro, which defaults to ``32`` bytes.
This default is conservative and safe for constrained embedded stacks.
The work buffer size **does not** need to be a power of 2.

Benchmark results on the 100MB enwik8 dataset on an Apple M3 MacBook Air:

+------------------------+--------------------+---------------------+
| Work Buffer Size (B)   | Compress Time (s)  | Decompress Time (s) |
+========================+====================+=====================+
| 32                     | 5.554              | 0.832               |
+------------------------+--------------------+---------------------+
| 64                     | 5.380              | 0.671               |
+------------------------+--------------------+---------------------+
| 128                    | 5.314              | 0.580               |
+------------------------+--------------------+---------------------+
| 256                    | 5.324              | 0.543               |
+------------------------+--------------------+---------------------+
| 512                    | 5.261              | 0.507               |
+------------------------+--------------------+---------------------+
| 1024                   | 5.273              | 0.514               |
+------------------------+--------------------+---------------------+

Stream Compression
------------------
First initialize a ``TampCompressor`` with ``tamp_compressor_init``, then use ``tamp_compress_stream``:

.. code-block:: c

   TampCompressor compressor;
   unsigned char window[1 << 10];

   // Initialize the compressor (NULL conf for defaults)
   tamp_compressor_init(&compressor, NULL, window);

   // Compress from source to destination
   tamp_res tamp_compress_stream(
       TampCompressor *compressor,   // Initialized compressor
       tamp_read_t read_cb,          // Callback to read uncompressed input
       void *read_handle,            // Handle passed to read_cb
       tamp_write_t write_cb,        // Callback to write compressed output
       void *write_handle,           // Handle passed to write_cb
       size_t *input_consumed_size,  // Output: total bytes read (may be NULL)
       size_t *output_written_size,  // Output: total bytes written (may be NULL)
       tamp_callback_t callback,     // Progress callback (may be NULL)
       void *user_data               // User data for callback
   );

The stream progress callback receives ``(input_consumed, 0)`` — ``total_bytes`` is 0 because the total input size is not known ahead of time.

Stream Decompression
--------------------
First initialize a ``TampDecompressor`` with ``tamp_decompressor_init``, then use ``tamp_decompress_stream``:

.. code-block:: c

   TampDecompressor decompressor;
   unsigned char window[1 << 10];

   // Initialize the decompressor (NULL conf to read from stream header)
   tamp_decompressor_init(&decompressor, NULL, window, 10);

   // Decompress from source to destination
   tamp_res tamp_decompress_stream(
       TampDecompressor *decompressor,  // Initialized decompressor
       tamp_read_t read_cb,             // Callback to read compressed input
       void *read_handle,               // Handle passed to read_cb
       tamp_write_t write_cb,           // Callback to write decompressed output
       void *write_handle,              // Handle passed to write_cb
       size_t *input_consumed_size,     // Output: total bytes read (may be NULL)
       size_t *output_written_size,     // Output: total bytes written (may be NULL)
       tamp_callback_t callback,        // Progress callback (may be NULL)
       void *user_data                  // User data for callback
   );

The stream progress callback receives ``(input_consumed, 0)``, same as compression streams.

Built-in I/O Handlers
---------------------
Tamp provides built-in read/write handlers for common use cases.
The same handlers work for both compression and decompression, and can be mixed (e.g., read from memory, write to file).
Enable them by defining the appropriate macro via compiler flags (e.g., ``-DTAMP_STREAM_STDIO=1``).

TAMP_STREAM_STDIO
~~~~~~~~~~~~~~~~~
Standard C stdio (``FILE*``).
Works with standard C library, ESP-IDF VFS, and any POSIX-compatible system.

.. code-block:: c

   // Compile with: -DTAMP_STREAM_STDIO=1

   FILE *in = fopen("input.txt", "rb");
   FILE *out = fopen("output.tamp", "wb");
   unsigned char window[1 << 10];

   TampCompressor compressor;
   tamp_compressor_init(&compressor, NULL, window);

   tamp_compress_stream(
       &compressor,             // initialized compressor
       tamp_stream_stdio_read,  // read callback
       in,                      // read handle
       tamp_stream_stdio_write, // write callback
       out,                     // write handle
       NULL,                    // input_consumed_size (optional)
       NULL,                    // output_written_size (optional)
       NULL,                    // progress callback (optional)
       NULL                     // callback user_data (optional)
   );

   fclose(in);
   fclose(out);

TAMP_STREAM_MEMORY
~~~~~~~~~~~~~~~~~~
Memory buffer handlers, typically used for file-to-memory or memory-to-file operations.
For memory-to-memory operations, use ``tamp_compressor_compress_and_flush`` or ``tamp_decompressor_decompress`` directly.

.. code-block:: c

   // Compile with: -DTAMP_STREAM_MEMORY=1 -DTAMP_STREAM_STDIO=1

   const unsigned char input_data[] = "Data to compress...";
   unsigned char window[1 << 10];

   // Setup memory reader for input
   TampMemReader reader = {
       .data = input_data,
       .size = sizeof(input_data),
       .pos = 0
   };

   // Initialize compressor
   TampCompressor compressor;
   tamp_compressor_init(&compressor, NULL, window);

   // Compress from memory to file
   FILE *out = fopen("output.tamp", "wb");
   tamp_compress_stream(
       &compressor,             // initialized compressor
       tamp_stream_mem_read,    // read callback (read plaintext from memory)
       &reader,                 // read handle
       tamp_stream_stdio_write, // write callback (write compressed data to file)
       out,                     // write handle
       NULL,                    // input_consumed_size (optional)
       NULL,                    // output_written_size (optional)
       NULL,                    // progress callback (optional)
       NULL                     // callback user_data (optional)
   );
   fclose(out);

TAMP_STREAM_LITTLEFS
~~~~~~~~~~~~~~~~~~~~
`LittleFS <https://github.com/littlefs-project/littlefs>`_ is a fail-safe filesystem designed for embedded systems.
Unlike stdio where a single ``FILE*`` handle is sufficient, LittleFS requires both the filesystem object (``lfs_t``) and the file handle (``lfs_file_t``) for I/O operations.
Tamp provides a ``TampLfsFile`` struct to bundle these together into a single handle.

.. code-block:: c

   // Compile with: -DTAMP_STREAM_LITTLEFS=1 -Ipath/to/littlefs

   lfs_t lfs;
   lfs_file_t in_file, out_file;
   // Assumes lfs_mount(&lfs, &cfg) has already been called
   lfs_file_open(&lfs, &in_file, "data.tamp", LFS_O_RDONLY);
   lfs_file_open(&lfs, &out_file, "data.bin", LFS_O_WRONLY | LFS_O_CREAT);

   // Bundle filesystem and file handle together
   TampLfsFile in_handle = { .lfs = &lfs, .file = &in_file };
   TampLfsFile out_handle = { .lfs = &lfs, .file = &out_file };
   unsigned char window[1 << 10];

   // Initialize decompressor (NULL conf reads from stream header)
   TampDecompressor decompressor;
   tamp_decompressor_init(&decompressor, NULL, window, 10);

   tamp_decompress_stream(
       &decompressor,           // initialized decompressor
       tamp_stream_lfs_read,    // read callback (read compressed data from file)
       &in_handle,              // read handle
       tamp_stream_lfs_write,   // write callback (write decompressed data to file)
       &out_handle,             // write handle
       NULL,                    // input_consumed_size (optional)
       NULL,                    // output_written_size (optional)
       NULL,                    // progress callback (optional)
       NULL                     // callback user_data (optional)
   );

   lfs_file_close(&lfs, &in_file);
   lfs_file_close(&lfs, &out_file);

TAMP_STREAM_FATFS
~~~~~~~~~~~~~~~~~
`FatFs <http://elm-chan.org/fsw/ff/>`_ (ChaN's FAT filesystem) is commonly used for SD cards and USB mass storage on embedded systems.
Unlike LittleFS, FatFs file handles (``FIL``) are self-contained and can be passed directly to the callbacks.

.. code-block:: c

   // Compile with: -DTAMP_STREAM_FATFS=1 -Ipath/to/fatfs

   FIL in_file, out_file;
   f_open(&in_file, "data.tamp", FA_READ);
   f_open(&out_file, "data.bin", FA_WRITE | FA_CREATE_ALWAYS);
   unsigned char window[1 << 10];

   // Initialize decompressor (NULL conf reads from stream header)
   TampDecompressor decompressor;
   tamp_decompressor_init(&decompressor, NULL, window, 10);

   tamp_decompress_stream(
       &decompressor,           // initialized decompressor
       tamp_stream_fatfs_read,  // read callback
       &in_file,                // read handle
       tamp_stream_fatfs_write, // write callback
       &out_file,               // write handle
       NULL,                    // input_consumed_size (optional)
       NULL,                    // output_written_size (optional)
       NULL,                    // progress callback (optional)
       NULL                     // callback user_data (optional)
   );

   f_close(&in_file);
   f_close(&out_file);
