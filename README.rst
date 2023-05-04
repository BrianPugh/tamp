.. image:: https://raw.githubusercontent.com/BrianPugh/tamp/main/assets/logo_300w.png
   :alt: tamp logo
   :width: 300
   :align: center

-----------------------------------------------------------------

|Python compat| |PyPi| |GHA tests| |Codecov report| |readthedocs|

.. inclusion-marker-do-not-remove

Tamp
====

Tamp is a low-memory, micropython-optimized, DEFLATE-inspired lossless compression library.

Features
========

* Pure python implementation.

* High compression ratios and low memory use.

* Small compression and decompression implementations.

* Mid-stream flushing.

* Customizable dictionary for greater compression of small messages.

* Convenient CLI interface.

Installation
============
Tamp contains 2 implementations: a desktop cpython implementation that is optimized for readability, and a micropython implementation that is optimized for runtime performance.

Desktop Python
^^^^^^^^^^^^^^
The Tamp library and CLI requires Python ``>=3.8`` and can be installed via:

.. code-block:: bash

   pip install tamp

MicroPython
^^^^^^^^^^^
For micropython use, there are 3 main files:

1. ``tamp/__init__.py`` - Always required.

2. ``tamp/decompressor_viper.py`` - Required for on-device decompression.

3. ``tamp/compressor_viper.py`` - Required for on-device compression.

For example, if on-device decompression isn't used, then do not include ``decompressor_viper.py``.
If manually installing, just copy these files to your microcontroller's ``/lib/tamp`` folder.

If using `mip`_, tamp can be installed by specifying the appropriate ``package-*.json`` file.

.. code-block:: bash

   mip install github:brianpugh/tamp  # Defaults to package.json: Compressor & Decompressor
   mip install github:brianpugh/tamp/package-compressor.json  # Compressor only
   mip install github:brianpugh/tamp/package-decompressor.json  # Decompressor only

If using `Belay`_, tamp can be installed by adding the following to ``pyproject.toml``.

.. code-block:: toml

   [tool.belay.dependencies]
   tamp = [
      "https://github.com/BrianPugh/tamp/blob/main/tamp/__init__.py",
      "https://github.com/BrianPugh/tamp/blob/main/tamp/compressor_viper.py",
      "https://github.com/BrianPugh/tamp/blob/main/tamp/decompressor_viper.py",
   ]

Usage
=====
Tamp works on desktop python and micropython. On desktop, Tamp is bundled with the
``tamp`` command line tool for compressing and decompressing tamp files.

CLI
^^^

Compression
-----------
Use ``tamp compress`` to compress a file or stream.
If no input file is specified, data from stdin will be read.
If no output is specified, the compressed output stream will be written to stdout.

.. code-block:: bash

   $ tamp compress --help

    Usage: tamp compress [OPTIONS] [INPUT_PATH]

    Compress an input file or stream.

   ╭─ Arguments ────────────────────────────────────────────────────────────────────────╮
   │   input_path      [INPUT_PATH]  Input file to compress or decompress. Defaults to  │
   │                                 stdin.                                             │
   ╰────────────────────────────────────────────────────────────────────────────────────╯
   ╭─ Options ──────────────────────────────────────────────────────────────────────────╮
   │ --output   -o      PATH                      Output file. Defaults to stdout.      │
   │ --window   -w      INTEGER RANGE [8<=x<=15]  Number of bits used to represent the  │
   │                                              dictionary window.                    │
   │                                              [default: 10]                         │
   │ --literal  -l      INTEGER RANGE [5<=x<=8]   Number of bits used to represent a    │
   │                                              literal.                              │
   │                                              [default: 8]                          │
   │ --help                                       Show this message and exit.           │
   ╰────────────────────────────────────────────────────────────────────────────────────╯


Example usage:

.. code-block:: bash

   tamp compress enwik8 -o enwik8.tamp  # Compress a file
   echo "hello world" | tamp compress | wc -c  # Compress a stream and print the compressed size.

The following options can impact compression ratios and memory usage:

* ``window`` -  ``2^window`` plaintext bytes to look back to try and find a pattern.
  A larger window size will increase the chance of finding a longer pattern match,
  but will use more memory, increase compression time, and cause each pattern-token
  to take up more space. Try smaller window values if compressing highly repetitive
  data, or short messages.

* ``literal`` - Number of bits used in each plaintext byte. For example, if all input
  data is 7-bit ASCII, then setting this to 7 will improve literal compression
  ratios by 11.1%. The default, 8-bits, can encode any binary data.

Decompression
-------------
Use ``tamp decompress`` to decompress a file or stream.
If no input file is specified, data from stdin will be read.
If no output is specified, the compressed output stream will be written to stdout.

.. code-block:: bash

  $ tamp decompress --help

  Usage: tamp decompress [OPTIONS] [INPUT_PATH]

  Decompress an input file or stream.

 ╭─ Arguments ────────────────────────────────────────────────────────────────────────╮
 │   input_path      [INPUT_PATH]  Input file. If not provided, reads from stdin.     │
 ╰────────────────────────────────────────────────────────────────────────────────────╯
 ╭─ Options ──────────────────────────────────────────────────────────────────────────╮
 │ --output  -o      PATH  Output file. Defaults to stdout.                           │
 │ --help                  Show this message and exit.                                │
 ╰────────────────────────────────────────────────────────────────────────────────────╯

Example usage:

.. code-block:: bash

   tamp decompress enwik8.tamp -o enwik8
   echo "hello world" | tamp compress | tamp decompress

Python
^^^^^^
The python library can perform one-shot compression, as well as operate on files/streams.

.. code-block:: python

   import tamp

   # One-shot compression
   string = b"I scream, you scream, we all scream for ice cream."
   compressed_data = tamp.compress(string)
   reconstructed = tamp.decompress(compressed_data)
   assert reconstructed == string

   # Streaming compression
   with tamp.open("output.tamp", "wb") as f:
       for _ in range(10):
           f.write(string)

   # Streaming decompression
   with tamp.open("output.tamp", "rb") as f:
       reconstructed = f.read()


Benchmark
=========
In the following section, we compare Tamp against:

* zlib_, a python builtin gzip-compatible DEFLATE compression library.

* heatshrink_, a data compression library for embedded/real-time systems.
  Heatshrink has similar goals as Tamp.

All of these are LZ-based compression algorithms, and tests were performed using a 1KB (10 bit) window.
Since zlib already uses significantly more memory by default, the lowest memory level (``memLevel=1``) was used in
these benchmarks. It should be noted that higher zlib memory levels will having greater compression ratios than Tamp.
Currently, there is no micropython-compatible zlib or heatshrink compression implementation, so these numbers are
provided simply as a reference.

Compression Ratio
^^^^^^^^^^^^^^^^^
The following table shows compression algorithm performance over a variety of input data sourced from the `Silesia Corpus`_
and Enwik8_. This should give a general idea of how these algorithms perform over a variety of input data types.

+-----------------------+-------------+----------------+----------------+------------+
| dataset               | raw         | tamp           | zlib           | heatshrink |
+=======================+=============+================+================+============+
| enwik8                | 100,000,000 | **51,635,633** | 56,205,166     | 56,110,394 |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/dickens | 10,192,446  | **5,546,761**  | 6,049,169      | 6,155,768  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/mozilla | 51,220,480  | 25,121,385     | **25,104,966** | 25,435,908 |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/mr      | 9,970,564   | 5,027,032      | **4,864,734**  | 5,442,180  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/nci     | 33,553,445  | 8,643,610      | **5,765,521**  | 8,247,487  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/ooffice | 6,152,192   | **3,814,938**  | 4,077,277      | 3,994,589  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/osdb    | 10,085,684  | **8,520,835**  | 8,625,159      | 8,747,527  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/reymont | 6,627,202   | **2,847,981**  | 2,897,661      | 2,910,251  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/samba   | 21,606,400  | 9,102,594      | **8,862,423**  | 9,223,827  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/sao     | 7,251,944   | **6,137,755**  | 6,506,417      | 6,400,926  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/webster | 41,458,703  | **18,694,172** | 20,212,235     | 19,942,817 |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/x-ray   | 8,474,240   | 7,510,606      | **7,351,750**  | 8,059,723  |
+-----------------------+-------------+----------------+----------------+------------+
| build/silesia/xml     | 5,345,280   | 1,681,687      | **1,586,985**  | 1,665,179  |
+-----------------------+-------------+----------------+----------------+------------+

Tamp usually out-performs heatshrink, and is generally very competitive with zlib.
While trying to be an apples-to-apples comparison, zlib still uses significantly more
memory during both compression and decompression (see next section). Tamp accomplishes
competitive performance while using around 10x less memory.

Memory Usage
^^^^^^^^^^^^
The following table shows approximately how much memory each algorithm uses during compression and decompression.

+---------------+-------------------+------------------------------+-------------------+
| Action        | tamp              | zlib                         | heatshrink        |
+===============+===================+==============================+===================+
| Compression   | (1 << windowBits) | (1 << (windowBits+2)) + 7 KB | (1 << windowBits) |
+---------------+-------------------+------------------------------+-------------------+
| Decompression | (1 << windowBits) | (1 << windowBits) + 7 KB     | (1 << windowBits) |
+---------------+-------------------+------------------------------+-------------------+

Both tamp and heatshrink have a few dozen bytes of overhead in addition to the primary window buffer, but are implementation-specific and ignored for clarity here.

Runtime
^^^^^^^
The desktop implementation is quite slow and could be significantly sped up (probably 1000x) with a C or Rust implementation.
However, Tamp inherently targets smaller files due to it's operating requirements, on the order of tens of megabytes or less.
Therefore, the slow runtime is still quite insignificant for small files.
If the desktop runtime performance becomes an issue for your use-case, please open up a Github issue and we can prioritize an optimized implementation.

When to use Tamp
================
On a Pi Pico (rp2040), the viper implementation of Tamp can compress data at around 4,300 bytes/s when using a 10-bit window. The data can then be decompressed at around 42,700 bytes/s.
Tamp is good for compressing data on-device. If purely decompressing data on-device, it will nearly always be better to use the micropython-builtin ``zlib.decompress``, when available.

.. |GHA tests| image:: https://github.com/BrianPugh/tamp/workflows/tests/badge.svg
   :target: https://github.com/BrianPugh/tamp/actions?query=workflow%3Atests
   :alt: GHA Status
.. |Codecov report| image:: https://codecov.io/github/BrianPugh/tamp/coverage.svg?branch=main
   :target: https://codecov.io/github/BrianPugh/tamp?branch=main
   :alt: Coverage
.. |readthedocs| image:: https://readthedocs.org/projects/tamp/badge/?version=latest
        :target: https://tamp.readthedocs.io/en/latest/?badge=latest
        :alt: Documentation Status
.. |Python compat| image:: https://img.shields.io/badge/>=python-3.8-blue.svg
.. |PyPi| image:: https://img.shields.io/pypi/v/tamp.svg
        :target: https://pypi.python.org/pypi/tamp
.. _Belay: https://github.com/BrianPugh/belay
.. _zlib:  https://docs.python.org/3/library/zlib.html
.. _heatshrink: https://github.com/atomicobject/heatshrink
.. _Silesia Corpus: https://sun.aei.polsl.pl//~sdeor/index.php?page=silesia
.. _Enwik8: https://mattmahoney.net/dc/textdata.html
.. _mip: https://docs.micropython.org/en/latest/reference/packages.html#installing-packages-with-mip
