========
Research
========

Run Length Encoding
===================
.. note::
   In this document, we will focus on the typical Tamp configuration: ``window=10``, ``literal=8``.

A limitation of tamp's encoding system is that it can only handle relatively short patterns.
Under most configurations, tamp maxes out at 15 bytes.

For the typical configuration (``window=10``, ``literal=8``), a 15-byte pattern takes :math:`1 + 6 + 10 = 17` bits.
15 bytes is 120 bits, so :math:`\frac{120}{17} = 7.0588` should be the theoretical maximum compression ratio of data with Tamp.

Let's confirm this:

.. code-block:: python

   import tamp

   data = b"\xff" * 1_000_000
   compressed = tamp.compress(data)
   print(f"Ratio: {len(data) / (len(compressed) - 1)}")  # subtract 1 for the header
   # Ratio: 7.0585

If we need to encode patterns longer than 15 bytes, then we need to produce another pattern token.

How common are long repeating runs?
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
TODO

Possible encoding schemes
^^^^^^^^^^^^^^^^^^^^^^^^^
Let's imagine our initial dictionary is all ``0x00``, and we wish to encode 1,000 bytes of `0xFF`.
How many writes do we need before we can take advantage of Tamp's full 15-bit pattern?

#. Write the ``0xFF`` literal (:math:`1 + 8 = 9` bits).
#. Write another ``0xFF`` literal (:math:`1 + 8 = 9` bits).
#. Write a 2-byte pattern match (:math:`1 + 1 + 10 = 12` bits).
#. Write a 4-byte pattern match (:math:`1 + 4 + 10 = 15` bits).
#. Write a 8-byte pattern match (:math:`1 + 6 + 10 = 17` bits).

This results in a total of 62 bits (7.75 bytes) until Tamp can be most efficient.
That means that the worst case (efficiency-wise) is attempting to encode 16 bytes.

Completely Rewrite the Huffman Table
""""""""""""""""""""""""""""""""""""
Currently, the huffman table looks like this:

.. code-block:: python

   huffman_coding = {
       0: 0b0,
       1: 0b11,
       2: 0b1000,
       3: 0b1011,
       4: 0b10100,
       5: 0b100100,
       6: 0b100110,
       7: 0b101011,
       8: 0b1001011,
       9: 0b1010100,
       10: 0b10010100,
       11: 0b10010101,
       12: 0b10101010,
       13: 0b100111,
       "FLUSH": 0b10101011,
   }

A possibility is that we could add a huffman code that states "the following window bits indicate how many times we should repeat the last-written-character to the window buffer."

Design considerations:

* There is currently 15 symbols in the huffman table; this is nice because it fits in 4 bits.
* The number of bits of each huffman code ranges from 1 to 8 bits. This range (:math:`[0, 7]`) can be represented by 3 bits.
* The packed symbol value + bit-length is 7 bits; this allows them to neatly fit in a uint8 array.
* When compressing data, we like to use a ``uint32_t`` bit buffer because it can efficiently handle bit-shifts.
  There may be up to 7 bits of data from a previous compression cycle in the bit buffer, resulting in only 25 bits free for the current compression cycle.
  With the maximum 15-bit window, a pattern match could be :math:`1 + 8 + 15 = 24` bits.
  This leaves 1 bit left free to play around with.
* Decompressing data has the same design constraints with regards to its ``uint32_t`` input buffer.

All of this is to say is that we could potentially add 1 bit of data to our maximum token writing while still maintaining a lot of our existing optimizations.
This could be used to extend the huffman codes by 1 bit (9 bits total) while still maintaining a lot of our optimizations.

**Pros:**

* Compact, efficient.
* Flexible if we want to encode any other new additional compression techniques.

**Cons:**

* Requires a completely different huffman lookup for decoding, potentially bloating the decoder by an additional ~150 bytes or so.
* Potentially introduces branching logic into the hot path for decoding.

Tweaking the Huffman Table
""""""""""""""""""""""""""
Instead of completely rewriting the Huffman table, what if we just tweak it a little bit.


Use an invalid offset to represent RLE
""""""""""""""""""""""""""""""""""""""
Because Tamp's window doesn't wrap, the final offset position isn't valid because a 2-byte match would overflow.
That means that we can give this offset value special meaning.

We can use the ``length`` field to represent the number of times to repeat the character.

Let's make the initial implementation "repeat the last character written to the window."
In the worst case scenario, this may introduce a 1-bit overhead that we can try to optimize out/solve later.

By the same logic of minimum-pattern-length for pattern matching, the minimum run-length in this situation would also be 2.
With this schema, we would be able to immediately ramp up to a 15-byte match.
For the previous 16-byte scenario (62 bits), we would now be able to do this in 26 bits, a significant improvement.

**Pros:**

* Is a strict enhancement on the current compression protocol, meaning that there are not any real tradeoffs with the current protocol.

**Cons:**

* Inefficient use of ``window`` bits.

Chaining RLE
^^^^^^^^^^^^

(TODO: Add content for this section)

Literal Streaks
^^^^^^^^^^^^^^^
Incompressible data will result in frequent streaks of literals. For each literal, we lose 1 bit of storage compared to the original uncompressed data.
Using the previous mentioned encoding scheme, we would need 13 or so literals in a row before we see some savings.
