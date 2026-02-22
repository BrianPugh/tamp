.. _migrating-to-v2:

Migrating to v2
===============
Tamp v2 introduces the extended compression format (RLE and extended match encoding)
and several API improvements.
This page covers what changed and how to update your code.

.. important::

   The extended format (``extended=True``) is **enabled by default** in v2.
   Data compressed with v2 defaults **cannot be decompressed by tamp <2.0.0**.
   Older decompressors will safely return an error or raise an exception (they validated
   that the now-used header bit was reserved).

   If you need to produce data readable by older tamp versions, explicitly disable the
   extended format:

   - C: ``TampConf conf = {... .extended = 0};``
   - Python: ``tamp.Compressor(f, extended=False)``
   - JavaScript: ``compress(data, { extended: false })``

Format Changes
^^^^^^^^^^^^^^
The v2 format is fully backwards-compatible for decompression: v2 decompressors
handle both v1 and v2 streams automatically. No changes are needed to decompress
existing v1 data.

Extended Format
---------------
When ``extended=True`` (the default), two new token types improve compression:

- **RLE (Run-Length Encoding)**: Efficiently encodes runs of repeated bytes.
  Up to 241 consecutive identical bytes per token.
- **Extended Match**: Allows pattern matches much longer than the v1 maximum of
  ``min_pattern_size + 13``, up to ``min_pattern_size + 131``.

See :doc:`specification` for encoding details.

Header Bit [1]
--------------
Bit [1] of the stream header, previously reserved (always 0), now indicates whether
the stream uses the extended format. v2 decompressors use this bit to transparently
handle both formats. Existing v1 decompressors validated that this bit was 0, so they
will safely return an error or raise an exception when encountering a v2 stream rather
than silently producing corrupt output.

C Library
^^^^^^^^^

Enabling Extended Format
------------------------
To produce v2 output, set ``extended = 1`` in the configuration.
Omitting ``.extended`` defaults to 0 (v1 format):

.. code-block:: c

   // v1 output (default, unchanged from before).
   TampConf conf = {.window = 10, .literal = 8};

   // v2 extended format (new default).
   TampConf conf = {.window = 10, .literal = 8, .extended = 1};

Callback Semantics
------------------
The ``tamp_callback_t`` progress callback now consistently passes **input bytes consumed**
as ``bytes_processed`` across all API functions.

Previously, the callback behavior was inconsistent:

.. code-block:: text

   v1 behavior:
     compress_cb:       (output_written, total_input)   -- mixed units
     compress_stream:   (input_consumed, 0)
     decompress_cb:     (output_written, input_size)    -- mixed units
     decompress_stream: (output_written, 0)

   v2 behavior (all functions):
     bytes_processed = input bytes consumed so far
     total_bytes     = total input size, or 0 for stream API

This means ``bytes_processed / total_bytes`` now gives a meaningful progress percentage
for the non-stream API. Callbacks used only for watchdog resets are unaffected.

If your callback relied on ``bytes_processed`` being output bytes written,
update it to expect input bytes consumed instead.

Struct Layout Changes
---------------------
``TampConf`` has a new ``extended`` bitfield inserted before ``lazy_matching``,
which changes the bit layout. Code that manipulates ``TampConf`` as raw bits
must be updated.

``TampCompressor`` and ``TampDecompressor`` have been reorganized and have new fields
for extended format state. ``sizeof()`` of both structs has increased.
Code that accesses struct internals directly (rather than through the API) must be updated.

New Compile-Time Flags
----------------------
The ``TAMP_EXTENDED`` family of flags controls extended format support:

- ``TAMP_EXTENDED`` (default ``1``): Master switch.
- ``TAMP_EXTENDED_COMPRESS`` (default ``TAMP_EXTENDED``): Compressor-only override.
- ``TAMP_EXTENDED_DECOMPRESS`` (default ``TAMP_EXTENDED``): Decompressor-only override.

No existing flags changed behavior. Setting ``-DTAMP_EXTENDED=0`` disables extended
format entirely, producing a v1-only build.

Installation
^^^^^^^^^^^^
The CLI is now an optional extra.
If you use the ``tamp`` command line tool, update your install command:

.. code-block:: bash

   pip install tamp[cli]

The core library (``pip install tamp``) no longer pulls in CLI dependencies.

CLI
^^^
The new ``--extended`` flag defaults to enabled. To produce v1 output:

.. code-block:: bash

   tamp compress --no-extended input.txt output.tamp

Decompression handles both v1 and v2 data automatically:

.. code-block:: bash

   tamp decompress output.tamp restored.txt

Python
^^^^^^
The new ``extended`` parameter defaults to ``True``:

.. code-block:: python

   # Produces v2 output by default.
   compressor = tamp.Compressor(f)

   # Explicitly produce v1 output for older decompressors.
   compressor = tamp.Compressor(f, extended=False)

JavaScript/WASM
^^^^^^^^^^^^^^^
The new ``extended`` option defaults to ``true``:

.. code-block:: javascript

   // Produces v2 output by default.
   const compressed = await compress(data);

   // Explicitly produce v1 output for older decompressors.
   const compressed = await compress(data, { extended: false });
