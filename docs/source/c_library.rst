.. _C Library:

C Library
=========
Tamp provides a C library optimized for low-memory-usage, fast runtime, and small binary footprint.
This page describes how to use the provided library.

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

   # include "tamp/compressor.h"

Initialization
--------------
All compression is performed using a ``TampCompressor`` object.
The object must first be initialized with ``tamp_compressor_init``.
The compressor object, a configuration, and a buffer is provided.
Tamp performs no internal allocations, so a buffer must be provided.
Tamp is an LZ-based compression schema, and this buffer is commonly called a "window buffer" or "dictionary".
The size of the provided buffer must be the same size as described by ``conf.window``.


.. code-block:: c

   static unsigned char *window_buffer[1024];

   TampConf conf = {
      /* Describes the size of the decompression buffer in bits.
      A 10-bit window represents a 1024-byte buffer.
      Must be in range [8, 15], representing [256, 32678] byte windows. */
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
      .use_custom_dictionary = false
   };
   TampCompressor compressor;
   tamp_compressor_init(&compressor, &conf, window_buffer);

   // TODO: use the initialized compressor object


Compression
-----------
Once the ``TampCompressor`` object is initialized, compression of data can be performed.
There's a low-level API to accomplish this, as well as a higher-level one.

The low-level workflow loop is as follows:

1. ``tamp_compressor_sink`` - Sink in a few bytes of data into the compressor's internal input buffer (16 bytes).

2. ``tamp_compressor_poll`` - Perform a single compression cycle, compressing up to 15 bytes from the input buffer.
   Compression is most efficient when the input buffer is full.

The sinking operation is computationally cheap, while the poll compression cycle is much more computationally intensive.
Breaking the operation up into these two functions allows ``tamp_compressor_poll`` to be called at a more opportune time in your program.

To use these 2 functions effectively, loop over calling ``tamp_compressor_sink``, then ``tamp_compressor_poll``.

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
            res = tamp_compressor_compress_poll(compressor, output, output_size, &chunk_output_written_size);
            output += chunk_output_written_size;
            output_size -= chunk_output_written_size;
            assert(res == TAMP_OK);
        }
    }

It is common to compress until an input buffer is exhausted, or an output buffer is full.
Tamp provides a higher level function, ``tamp_compressor_compress`` that does exactly this.
Note: you may actually want to use ``tamp_compressor_compress_flush``, described in the next section.

Flushing
--------
Inside the compressor, there may be up to 16 **bytes** of uncompressed data in the input buffer, and 31 **bits** in an output buffer.
This means that the compressed output lags behind the input data stream.

For example, if we compress the 44-long non-null-terminated string ``"The quick brown fox jumped over the lazy dog"``,
the compressor will produce a 32-long data stream, that decompresses to ``"The quick brown fox jumped ov"``.
The remaining ``"er the lazy dog"`` is still in the compressor's internal buffers.

To flush the remaining data, use ``tamp_compressor_flush`` that performs the following actions:

1. Repeatedly call ``tamp_compressor_compress_poll`` until the 16-byte internal input buffer is empty.

2. Flush the output buffer. If ``write_token=true``, then the special ``FLUSH`` token will be appended if padding was required.

.. code-block:: c

   tamp_res res;
   output_buffer = bytes[100];
   size_t output_written;  // Stores the resulting number of bytes written to output_buffer.

   res = tamp_compressor_flush(&compressor, output_buffer, sizeof(output_buffer), &output_written, true);
   assert(res == TAMP_OK);

The special ``FLUSH`` token allows for the compressor to continue being used, but adds 0~2 bytes of overhead.

1. If intending to continue using the compressor object, then ``write_token`` should be true.

2. If flushing the compressor to finalize a stream, then setting ``write_token`` to false will save 0~2 bytes.
   Setting ``write_token`` to true will have no impact aside from the extra 0~2 byte overhead.

``tamp_compressor_compress_and_flush`` is just like ``tamp_compressor_compress``, with the addition that the
internal buffers are flushed at the end of the call.

Summary
-------

.. code-block:: c

   unsigned char *window_buffer[1024];
   const unsigned char input_string[44] = "The quick brown fox jumped over the lazy dog";
   unsigned char output_buffer[64];

   TampConf conf = {.window=10, .literal=8};
   TampCompressor compressor;
   tamp_compressor_init(&compressor, &conf, window_buffer);

   size_t input_consumed_size, output_written_size;
   tamp_compressor_compress_and_flush(
        &compressor,
        input_string, sizeof(input_string), &input_consumed_size,
        output_buffer, sizeof(output_buffer), &output_written_size
   );

   // Compressed data is now in output_buffer
   printf("Compressed size: %d\n", output_written_size);


Decompressor
^^^^^^^^^^^^
The decompressor API is much simpler than the compressor API.
To include the decompressor functionality, include the decompressor header:

.. code-block:: c

   # include "tamp/decompressor.h"

Initialization
--------------
All decompression is performed using a ``TampDecompressor`` object.
Like ``TampCompressor``, this object needs to be configured with a ``TampConf`` object.
Typically, this configuration comes from the Tamp header at the beginning of the compressed data.
Use ``tamp_decompressor_read_header`` to read the header into a ``TampConf``:

.. code-block:: c

   const unsigned char compressed_data[64];  // Imagine this contains tamp-compressed data.
   sizez_t compressed_data_size = 64;
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
   res = tamp_decompressor_init(&decompressor, NULL, window_buffer);

   assert(res == TAMP_OK);

Decompression
-------------
Data decompression is straight forward:

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
   // In all situations, output_written_size and input_consumed_size is updated.
