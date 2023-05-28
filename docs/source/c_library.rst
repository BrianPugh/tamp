C Library
=========
Tamp provides a C library optimized for low-memory-usage, fast runtime, and small binary footprint.
This page describes how to use the provided library.

Overview
^^^^^^^^
To use Tamp in your C project, simply copy the contents of ``tamp/_c_src`` into your project.
The contents are broken down as follows (header files described, but you'll also need the c files):

1. ``tamp/common.h`` - Common functionality needed by both the compressor and decompressor. Must also be included.

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
      Must be in range [8, 15], representing [256, 32678] bytes. */
      .window = 10,

      /* Number of bits occupied in each plaintext symbol.
      For example, if ASCII text is being encoded, then we could set this
      value to 7 to slightly improve compression ratios.
      For most use-cases, 8 (the whole byte) is appropriate. */
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

The low-level workflow is as follows:

1. ``tamp_compressor_sink`` - Sink in a few bytes of data into the compressor's input buffer (16 bytes).

2. ``tamp_compressor_poll`` - Perform a single compression cycle, compressing possibly up to ~15 bytes from the input buffer.
                              Compression is most efficient when the input buffer is full.

The sinking operation is computationally cheap; but the poll compression cycle is much more computationally intensive.
Breaking the operation up into these two functions allows ``sink_compressor_poll`` to be optionally called at a more opportune time in your program.


To use these 2 functions effectively, we'll loop over calling ``tamp_compressor_sink``, then ``tamp_compressor_poll``.

.. code-block:: c

    while(input_size > 0 && output_size > 0){
        {
            // Sink Data into input buffer.
            size_t consumed;
            tamp_compressor_sink(compressor, input, input_size, &consumed);
            input += consumed;
            input_size -= consumed;
        }
        if(compressor->input_size == sizeof(compressor->input)){
            // Input buffer is full and ready to start compressing.
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
Inside the compressor, there may be up to 16 bytes in the input buffer, and ~7 bits in an output buffer.
This means that the compressed output lags behind the input data stream.

For example, if we compress the 44-long non-null-terminated string ``"The quick brown fox jumped over the lazy dog"``,
the compressor will produce a 32-long data stream, that decompresses to ``"The quick brown fox jumped ov"``.
The remaining ``"er the lazy dog"`` is still in the compressor's internal buffers.

To flush the remaining data, use ``tamp_compressor_flush`` that performs the following actions:

1. Repeatedly call ``tamp_compressor_compress_poll`` until the 16-byte input buffer is empty.

2. Flush the output buffer.
   If the output buffer is empty, then return.
   If ``write_token=true``, then append the special ``FLUSH`` token.
   Finally, zero-pad the remainder of the output buffer and flush.


.. code-block:: c

   tamp_res res;
   output_buffer = bytes[100];
   size_t output_written;  // Stores the resulting number of bytes written to output_buffer.

   res = tamp_compressor_flush(&compressor, output_buffer, sizeof(output_buffer), &output_written, true);
   assert(res == TAMP_OK);

The special ``FLUSH`` token allows for the compressor to continue being used, but adds 1~2 bytes of overhead.

1. If intending to continue using the compressor object, then ``write_token`` should be true.

2. If flushing the compressor to finalize a stream, then setting ``write_token`` to false will save 1~2 bytes.
   Setting ``write_token`` to true will have no impact aside from the extra 1~2 byte overhead.

``tamp_compressor_compress_and_flush`` is just like ``tamp_compressor_compress``, with the addition that the
internal buffers are flushed at the end of the call (with ``write_token`` set to false).

Summary
-------

.. code-block:: c


Decompressor
^^^^^^^^^^^^
The decompressor API is much simpler than the compressor API.
To include the compressor functionality, include the decompressor header:

.. code-block:: c

   # include "tamp/decompressor.h"
