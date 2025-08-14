========
Research
========

.. raw:: html

   <style>
   .good { color: #28a745; font-weight: bold; }
   .bad { color: #dc3545; font-weight: bold; }
   .neutral { color: #ffc107; font-weight: bold; }
   </style>

.. role:: good
   :class: good

.. role:: bad
   :class: bad

.. role:: neutral
   :class: neutral

This page is a collection of ideas on how to improve Tamp.

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
-----------------------------------

To get an idea of how often repeating characters are in the wild, we'll look at two examples:

* ``enwik8`` - 100MB of wikipedia data.
* ``RPI_PICO-20250415-v1.25.0.uf2`` - A popular firmware for the raspberry pi pico microcontroller.

enwik8
^^^^^^
The enwik8 dataset is used as a proxy of "human knowledge" from the `Hutter Prize <http://prize.hutter1.net/>`_ competition site.
It is used as a good balance of different types of data a typical compression algorithm might run into.

.. image:: ../../assets/enwik8-RLE-v1.10.0.png
   :alt: enwik8 RLE analysis showing run length distribution
   :align: center

A majority of the run-lengths are in the 2-50 range.

MicroPython Firmware
^^^^^^^^^^^^^^^^^^^^^
The MicroPython firmware (v1.25.0) for the raspberry pi pico (rp2040) represents a typical compiled binary that a microcontroller might run into.
For example, OTA firmware updates might be compressed to be efficiently transferred between devices.
Compiled programs are much different than the text-heavy enwik8 dataset.
It also has a lot of zero-padding which lends itself well to compression algorithms that can perform RLE or generally handle large patterns.

.. image:: ../../assets/RPI_PICO-20250415-v1.25.0-RLE-v1.10.0.png
   :alt: RPI PICO RLE analysis showing run length distribution
   :align: center

Due to the UF2 encoding, there are many long streams of ``0x00``. In fact, there's around 1300 occurrences of this ~220 in length. This offers a huge opportunity for RLE to significantly compress the data.

Possible encoding schemes
-------------------------
Let's imagine our initial dictionary is all ``0x00``, and we wish to encode 1,000 bytes of ``0xFF``.
How many writes do we need before we can take advantage of Tamp's full 15-bit pattern?

#. Write the ``0xFF`` literal (:math:`1 + 8 = 9` bits).
#. Write another ``0xFF`` literal (:math:`1 + 8 = 9` bits).
#. Write a 2-byte pattern match (:math:`1 + 1 + 10 = 12` bits).
#. Write a 4-byte pattern match (:math:`1 + 4 + 10 = 15` bits).
#. Write a 8-byte pattern match (:math:`1 + 6 + 10 = 17` bits).

This results in a total of 62 bits (7.75 bytes) until Tamp can be most efficient.
That means that the worst case (efficiency-wise) is attempting to encode 16 bytes.
A proposed schema needs to significantly improve this situation.

Completely Rewrite the Huffman Table
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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

A possibility is that we could add a huffman code that states "the following N bits indicate how many times we should repeat the last-written-character to the window buffer."

Design considerations:

* There is currently 15 symbols in the huffman table; this is nice because it fits in 4 bits.
* The number of bits of each huffman code ranges from 1 to 8 bits. This range (:math:`[0, 7]`) can be represented by 3 bits.
* The packed symbol value + bit-length is 7 bits; this allows them to neatly fit in a uint8 array.
* When compressing data, we like to use a ``uint32_t`` bit buffer because it can efficiently handle bit-shifts.
  There may be up to 7 bits of data from a previous compression cycle in the bit buffer, resulting in only 25 bits free for the current compression cycle.
  With the maximum 15-bit window, a pattern match could be :math:`1 + 8 + 15 = 24` bits.
  This leaves 1 bit left free to play around with.
* Decompressing data has the same design constraints with regards to its ``uint32_t`` input buffer.
* A fixed 8-bits indicating size seems sufficient; this would be able to represent lengths in range ``[2, 257]``. We can tweak this range by doing a similar computation that we do for ``min_pattern_size``.

All of this is to say is that we could potentially add 1 bit of data to our maximum token writing while still maintaining a lot of our existing optimizations.
This could be used to extend the huffman codes by 1 bit (9 bits total) while still maintaining a lot of our optimizations.

**Pros:**

* Compact, efficient.
* Flexible if we want to encode any other new additional compression techniques.

**Cons:**

* Requires a completely different huffman lookup for decoding, potentially bloating the decoder by an additional ~150 bytes or so.

Tweaking the Huffman Table
^^^^^^^^^^^^^^^^^^^^^^^^^^
Instead of completely rewriting the Huffman table, what if we just tweak it a little bit.
What if we remap the meaning of "12" to "do some RLE stuff"?
This would change the meaning of "13" to "12", but that can be done easily with non-branching logic:

.. code-block:: c

   if(TAMP_UNLIKELY(huffman_code == 12)){
       // This is the branching path; do RLE stuff here.
   }
   else{
       // Where use_rle is a bool
       huffman_code -= (conf->use_rle && huffman_code == 13)
   }

Here we can see that the code-cost is tiny, and it should have negligible performance impact on decoding.


**Pros:**

* Compact, efficient.
* **Very** compatible with current code base.

**Cons:**

* Reduces maximum pattern-match length from (typ.) 15 down to 14.

Use an invalid offset to represent RLE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Because Tamp's window doesn't wrap, the final offset position isn't valid because a 2-byte match would overflow.
That means that we can give this offset value special meaning.

We can use the ``length`` field to represent the number of times to repeat the character.

Let's make the initial implementation "repeat the last character written to the window."
In the worst case scenario, this may introduce a 1-bit overhead that we can try to optimize out/solve later.

By the same logic of minimum-pattern-length for pattern matching, the minimum run-length in this situation would also be 2.
With this schema, we would be able to immediately ramp up to a 15-byte match.
For the previous 16-byte scenario (62 bits), we would now be able to do this in 26 bits, a significant improvement.

However, this limits us to a 15-byte RLE. We can trade off precision for greater range. We could fine tune a non-linear mapping like the following:

.. code-block:: python

   mapping = {
       0: 2,
       1: 4,
       2: 6,
       3: 8,
       4: 10,
       5: 14,  # The previous literal plus this can now have a follow-up 15-pattern match.
       6: 30,
       7: 40,
       8: 60,
       9: 80,
       10: 100,
       11: 130,
       12: 160,
       13: 200,
   }

**Pros:**

* Is a strict enhancement on the current compression protocol, meaning that there are not any real tradeoffs with the current protocol.

**Cons:**

* Inefficient use of ``window`` bits.

Literal Streaks
---------------
Incompressible data will result in frequent streaks of literals. For each literal, we lose 1 bit of storage compared to the original uncompressed data.

Let's take a look at the histograms of how many literal tokens are emitted in a row with Tamp.

.. image:: ../../assets/enwik8-literal-run-lengths.png
   :alt: enwik8 analysis showing how many "literal" tokens are emitted in a row.
   :align: center

.. image:: ../../assets/RPI_PICO-20250415-v1.25.0-literal-run-lengths-v1.10.0.png
   :alt: Micropython firmware analysis showing how many "literal" tokens are emitted in a row.
   :align: center

If we had some sort of signal that says "the next X bytes are literals", we could potentially save some overhead in emitting a bunch of literals in a row. However, in our typical schema where we might assign an 8-bit huffman code to such an occurrence, we already immediately have a 9-bit overhead. If we want to be able to specify 5 bits to length, this would result in being able to represent sizes in range [15, 46].

On one end of the spectrum, 15, we only save 1 bit. On the other end of spectrum, 46, we save 32 bits (4 bytes). Consecutive literals in this length range are not that frequent, making this optimization not very attractive. Additionally, we would have to store an additional 46 bytes or so of memory to support this feature, since we would have to buffer literal output writes (and it would also make the output writes more complicated!).

Implementation
--------------
First thing's first: how determental is it to reduce the max-pattern-length from 15 to 14? This test disables the "12" huffman code and remaps "13"->"12".


+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| dataset                             | raw         | tamp (max-pattern=15)          | tamp (max-pattern=14)  | Degradation    |
+=====================================+=============+================================+========================+================+
| enwik8                              | 100,000,000 | 51,635,633 (**1.937**)         | 51,761,521 (**1.932**) | 0.244%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)          | 5,550,021 (**1.836**)  | 0.059%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)         | 25,374,814 (**2.019**) | 1.009%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)          | 5,054,346 (**1.973**)  | 0.543%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)          | 8,857,056 (**3.788**)  | 2.469%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)          | 3,822,445 (**1.609**)  | 0.197%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)          | 8,527,578 (**1.183**)  | 0.079%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)          | 2,852,894 (**2.323**)  | 0.173%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)          | 9,210,905 (**2.346**)  | 1.190%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)          | 6,137,755 (**1.182**)  | 0.000%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)         | 18,812,015 (**2.204**) | 0.630%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)          | 7,510,606 (**1.128**)  | 0.000%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)          | 1,711,843 (**3.123**)  | 1.793%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+
| build/RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)            | 334,256 (**1.997**)    | 0.889%         |
+-------------------------------------+-------------+--------------------------------+------------------------+----------------+

Generally, the degradation is fairly small and not large enough to dissuade further research/implementation.

This experiment raises a question: what if we instead disallowed 14-byte matches, downmapping them to 13-bytes? We then keep the 15-byte max-pattern length.

+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| dataset                             | raw         | tamp (max-pattern=15)          | tamp (max-pattern=14)  | tamp (no 14)           |
+=====================================+=============+================================+========================+========================+
| enwik8                              | 100,000,000 | 51,635,633 (**1.937**)         | 51,761,521 (**1.932**) | 51,700,012 (**1.934**) |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)          | 5,550,021 (**1.836**)  | 5,548,693 (**1.837**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)         | 25,374,814 (**2.019**) | 25,211,896 (**2.032**) |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)          | 5,054,346 (**1.973**)  | 5,027,142 (**1.983**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)          | 8,857,056 (**3.788**)  | 8,660,810 (**3.874**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)          | 3,822,445 (**1.609**)  | 3,818,583 (**1.611**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)          | 8,527,578 (**1.183**)  | 8,521,635 (**1.184**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)          | 2,852,894 (**2.323**)  | 2,850,157 (**2.325**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)          | 9,210,905 (**2.346**)  | 9,129,316 (**2.367**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)          | 6,137,755 (**1.182**)  | 6,137,762 (**1.182**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)         | 18,812,015 (**2.204**) | 18,726,007 (**2.214**) |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)          | 7,510,606 (**1.128**)  | 7,510,606 (**1.128**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)          | 1,711,843 (**3.123**)  | 1,689,975 (**3.163**)  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+
| build/RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)            | 334,256 (**1.997**)    | 331,397 (**2.015**)    |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+

So clearly it's better to just drop the "12" symbol, downmapping it to "11".

But now this raises the general question, **is there a better nonlinear mapping?** Downmapping 12->11 is one specific little tweak, but we could be much more general about it.
We're already introducing a breaking change, we can probably get more out of it.
However, since we don't want to confound longer-pattern-matching wins with matches that could be better performed with RLE, we'll have to shelf that thought for now and implement the rest of the RLE feature.

+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| dataset                             | raw         | tamp (max-pattern=15)          | tamp (no 14)           | tamp (rle)             | RLE Improvement     |
+=====================================+=============+================================+========================+========================+=====================+
| enwik8                              | 100,000,000 | 51,635,633 (**1.937**)         | 51,700,012 (**1.934**) | 51,804,615 (**1.930**) | :neutral:`-0.327%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)          | 5,548,693 (**1.837**)  | 5,548,526 (**1.837**)  | :neutral:`-0.032%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)         | 25,211,896 (**2.032**) | 24,984,172 (**2.050**) | **+0.546%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)          | 5,027,142 (**1.983**)  | 4,683,676 (**2.129**)  | :good:`+6.830%`     |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)          | 8,660,810 (**3.874**)  | 8,836,178 (**3.797**)  | :bad:`-2.228%`      |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)          | 3,818,583 (**1.611**)  | 3,819,752 (**1.611**)  | :neutral:`-0.126%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)          | 8,521,635 (**1.184**)  | 8,503,083 (**1.186**)  | **+0.208%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)          | 2,850,157 (**2.325**)  | 2,856,347 (**2.320**)  | :neutral:`-0.294%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)          | 9,129,316 (**2.367**)  | 8,952,213 (**2.414**)  | :good:`+1.652%`     |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)          | 6,137,762 (**1.182**)  | 6,137,059 (**1.182**)  | **+0.011%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)         | 18,726,007 (**2.214**) | 18,726,051 (**2.214**) | :neutral:`-0.171%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)          | 7,510,606 (**1.128**)  | 7,513,126 (**1.128**)  | :neutral:`-0.034%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)          | 1,689,975 (**3.163**)  | 1,688,915 (**3.165**)  | :neutral:`-0.430%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)            | 331,397 (**2.015**)    | 297,561 (**2.244**)    | :good:`+10.187%`    |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+

As expected, the RLE encoding significantly improved the ``RPI_PICO-20250415-v1.25.0.uf2`` compression.
If we weigh each of these files equally, then RLE offers a 1.128% average improvement over the base Tamp algorithm.
Any situations where it performed worse than (no 14) is more of a luck/probability distribution where a greedy-matcher sometimes performed better/worse.

But wait a minute! In the current implementation, we just encoded any run length >=2 as RLE.
The RLE encoding has fixed length of 17 bits, so it seems natural that sequential literals 2 or longer (18 bits) are smaller using the RLE encoding.
And this is true!
However, for shorter RLE, it's possible that a **pattern match is shorter.**

For the assumed ``window=10, literal=8`` scenario:

* Pattern-length 2: 12 bits
* Pattern-length 3: 13 bits
* Pattern-length 4: 15 bits
* Pattern-length 5: 15 bits
* Pattern-length 6: 16 bits
* Pattern-length 7: 17 bits
* Pattern-length 8: 17 bits
* Pattern-length 9: 17 bits
* Pattern-length 10: 18 bits

So for this configuration, if the RLE is 9 characters or shorter, than it's **possible** that a pattern match is shorter, if the pattern exists in the window buffer.

+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| dataset                             | raw         | tamp (max-pattern=15)          | tamp (no 14)           | tamp (rle)             | RLE Improvement     |
+=====================================+=============+================================+========================+========================+=====================+
| enwik8                              | 100,000,000 | 51,635,633 (**1.937**)         | 51,700,012 (**1.934**) | 51,770,803 (**1.932**) | :neutral:`-0.262%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)          | 5,548,693 (**1.837**)  | 5,548,508 (**1.837**)  | :neutral:`-0.031%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)         | 25,211,896 (**2.032**) | 24,965,695 (**2.052**) | **+0.620%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)          | 5,027,142 (**1.983**)  | 4,677,094 (**2.132**)  | :good:`+6.961%`     |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)          | 8,660,810 (**3.874**)  | 8,769,703 (**3.826**)  | :bad:`-1.459%`      |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)          | 3,818,583 (**1.611**)  | 3,815,564 (**1.612**)  | :neutral:`-0.016%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)          | 8,521,635 (**1.184**)  | 8,501,902 (**1.186**)  | **+0.222%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)          | 2,850,157 (**2.325**)  | 2,851,677 (**2.324**)  | :neutral:`-0.130%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)          | 9,129,316 (**2.367**)  | 8,940,797 (**2.417**)  | :good:`+1.777%`     |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)          | 6,137,762 (**1.182**)  | 6,137,003 (**1.182**)  | **+0.012%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)         | 18,726,007 (**2.214**) | 18,725,916 (**2.214**) | :neutral:`-0.170%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)          | 7,510,606 (**1.128**)  | 7,511,115 (**1.128**)  | :neutral:`-0.007%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)          | 1,689,975 (**3.163**)  | 1,688,422 (**3.166**)  | :neutral:`-0.400%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)            | 331,397 (**2.015**)    | 296,512 (**2.252**)    | :good:`+10.503%`    |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+

With this additional optimization, our improvement is now an average of 1.259% improvement over the baseline Tamp algorithm.

It also highlights another optimization: we really do not benefit from having more than 9 consecutive same-character values in a row in our window buffer.
In extreme cases, long single-character sequences could fill up the whole buffer, making subsequent pattern matching much less efficient.
So let's update it so that whenever RLE is written, only a maximum of 8 bytes are written to the window buffer (resulting in 9 consecutive same-character values).
if more than one consecutive RLE are performed, do not write anything to the window buffer for subsequent RLE token(s).

+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| dataset                             | raw         | tamp (max-pattern=15)          | tamp (no 14)           | tamp (rle)             | RLE Improvement     |
+=====================================+=============+================================+========================+========================+=====================+
| enwik8                              | 100,000,000 | 51,635,633 (**1.937**)         | 51,700,012 (**1.934**) | 51,770,464 (**1.932**) | :neutral:`-0.261%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)          | 5,548,693 (**1.837**)  | 5,548,383 (**1.837**)  | :neutral:`-0.029%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)         | 25,211,896 (**2.032**) | 24,947,393 (**2.053**) | :good:`+0.693%`     |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)          | 5,027,142 (**1.983**)  | 4,513,934 (**2.209**)  | :good:`+10.207%`    |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)          | 8,660,810 (**3.874**)  | 8,769,797 (**3.826**)  | :bad:`-1.460%`      |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)          | 3,818,583 (**1.611**)  | 3,815,217 (**1.613**)  | :neutral:`-0.007%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)          | 8,521,635 (**1.184**)  | 8,501,902 (**1.186**)  | **+0.222%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)          | 2,850,157 (**2.325**)  | 2,851,673 (**2.324**)  | :neutral:`-0.130%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)          | 9,129,316 (**2.367**)  | 8,851,097 (**2.441**)  | :good:`+2.763%`     |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)          | 6,137,762 (**1.182**)  | 6,137,003 (**1.182**)  | **+0.012%**         |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)         | 18,726,007 (**2.214**) | 18,725,719 (**2.214**) | :neutral:`-0.169%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)          | 7,510,606 (**1.128**)  | 7,510,925 (**1.128**)  | :neutral:`-0.004%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)          | 1,689,975 (**3.163**)  | 1,687,859 (**3.167**)  | :neutral:`-0.367%`  |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+
| build/RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)            | 331,397 (**2.015**)    | 287,917 (**2.319**)    | :good:`+13.097%`    |
+-------------------------------------+-------------+--------------------------------+------------------------+------------------------+---------------------+

With this additional optimization, our improvement is now an average of 1.755% improvement over the baseline Tamp algorithm.

The next thing to tweak is our limited pattern-match size.
Using a similar technique to RLE where we repurposed the "+12" symbol to indicate RLE, we can repurpose the "+13" symbol to say "a match greater than +11".
We can then tack on a few bits after the ``window`` bits that indicate how much greater than ``+12`` is it.

For the assumed ``window=10, literal=8`` scenario:

* 5 bits could encode matches up to 45 in length.
* 6 bits could encode matches up to 77 in length.
* 7 bits could encode matches up to 141 in length.

+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| dataset                       | raw         | tamp (max-pattern=15)    | tamp (rle + 5bit extension)   | tamp (rle + 6bit extension)   | 5bit Improvement    | 6bit Improvement    |
+===============================+=============+==========================+===============================+===============================+=====================+=====================+
| enwik8                        | 100,000,000 | 51,635,633 (**1.937**)   | 51,139,175 (**1.955**)        | 51,139,758 (**1.955**)        | **+0.961%**         | **+0.960%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)    | 5,538,050 (**1.840**)         | 5,539,912 (**1.840**)         | **+0.157%**         | **+0.123%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)   | 24,223,360 (**2.115**)        | 24,257,350 (**2.112**)        | :good:`+3.575%`     | :good:`+3.439%`     |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)    | 4,514,188 (**2.209**)         | 4,514,858 (**2.208**)         | :good:`+10.202%`    | :good:`+10.188%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)    | 7,263,576 (**4.619**)         | 6,907,163 (**4.858**)         | :good:`+15.966%`    | :good:`+20.089%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)    | 3,776,340 (**1.629**)         | 3,777,687 (**1.629**)         | **+1.012%**         | **+0.976%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)    | 8,474,181 (**1.190**)         | 8,475,184 (**1.190**)         | **+0.548%**         | **+0.536%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)    | 2,823,453 (**2.347**)         | 2,827,386 (**2.344**)         | **+0.861%**         | **+0.723%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)    | 8,440,584 (**2.560**)         | 8,437,537 (**2.561**)         | :good:`+7.273%`     | :good:`+7.306%`     |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)    | 6,137,330 (**1.182**)         | 6,137,330 (**1.182**)         | **+0.007%**         | **+0.007%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)   | 18,175,243 (**2.281**)        | 18,224,796 (**2.275**)        | :good:`+2.776%`     | :good:`+2.511%`     |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)    | 7,510,986 (**1.128**)         | 7,510,986 (**1.128**)         | :neutral:`-0.005%`  | :neutral:`-0.005%`  |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)    | 1,512,172 (**3.535**)         | 1,496,263 (**3.572**)         | :good:`+10.080%`    | :good:`+11.026%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+
| RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)      | 287,457 (**2.323**)           | 288,649 (**2.313**)           | :good:`+13.236%`    | :good:`+12.876%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+-------------------------------+---------------------+---------------------+

Using a 5-bit extension results in an average 4.761% improvement.
Using a 6-bit extension results in an average 5.054% improvement.
Generally, it seems like 6-bit could sometimes result in slightly worse compression ratios, but for some datasets it can signiciantly improve compression ratios.
If we ignore the ``nci`` dataset, the average performances between 5-bit and 6-bit are pretty much identical.

However, instead of using a simple 6-bit encoding, what if we also huffman encoded this extended match value?
We can experiment with different zipf parameters to see what works. For initial experiments, let's just benchmark against

+----------------+------------+-------------------------------+
| Zipf Parameter | Enwik8     | RPI_PICO-20250415-v1.25.0.uf2 |
+================+============+===============================+
| 0.3            | 51,105,740 | 288,531                       |
+----------------+------------+-------------------------------+
| 0.5            | 51,076,396 | 288,514                       |
+----------------+------------+-------------------------------+
| 0.7            | 51,061,063 | 288,501                       |
+----------------+------------+-------------------------------+
| 0.9            | 51,049,385 | 288,414                       |
+----------------+------------+-------------------------------+
| 1.059          | 51,047,479 | 288,423                       |
+----------------+------------+-------------------------------+
| 1.2            | 51,044,612 | 288,503                       |
+----------------+------------+-------------------------------+
| 1.5            | 51,066,476 | 288,704                       |
+----------------+------------+-------------------------------+

The value at ``1.059`` is special because it's the highest value that still has all the huffman codes be 8bits or less.
This is important because it allows us to write a more efficient encoder and decoder.
Also, from this limited test, it generally seems like a good value. Testing this value on the rest of the dataset:

+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| dataset                       | raw         | tamp (max-pattern=15)    | tamp (huffman s=1.059)        | Improvement         |
+===============================+=============+==========================+===============================+=====================+
| enwik8                        | 100,000,000 | 51,635,633 (**1.937**)   | 51,047,479 (**1.959**)        | **+1.139%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)    | 5,536,058 (**1.841**)         | **+0.193%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)   | 24,097,928 (**2.126**)        | :good:`+4.074%`     |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)    | 4,513,670 (**2.209**)         | :good:`+10.212%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)    | 6,911,598 (**4.855**)         | :good:`+20.038%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)    | 3,771,953 (**1.631**)         | **+1.127%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)    | 8,472,175 (**1.190**)         | **+0.571%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)    | 2,821,275 (**2.349**)         | **+0.938%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)    | 8,420,306 (**2.566**)         | :good:`+7.496%`     |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)    | 6,137,327 (**1.182**)         | **+0.007%**         |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)   | 18,166,442 (**2.282**)        | :good:`+2.823%`     |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)    | 7,510,986 (**1.128**)         | :neutral:`-0.005%`  |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)    | 1,486,723 (**3.595**)         | :good:`+11.593%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+
| RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)      | 288,423 (**2.315**)           | :good:`+12.945%`    |
+-------------------------------+-------------+--------------------------+-------------------------------+---------------------+

The huffman encoding approach achieves an average improvement of 5.225% over the baseline Tamp algorithm across all datasets.

The more general question is "is the additional compression ratios worth it for another huffman code?"
1. Compressor would need to store a ~120 byte lookup table (exact size depending on if we tweak the zipf parameters further).
2. Decompressor would need a 256 byte lookup table.

We're looking at probably ~600 bytes of firmware overhead to add huffman encoding for the extended bits over just using normal binary encoding.
If we could reuse our existing huffman tables, then all of a sudden this compression looks more attractive.

What if we do the encoding as follows:
1. The first 2 bits represent the 2 LSb of the decoded value.
2. The following huffman code represents The next 4 bits.
3. Finally, we add :math:`min_pattern_size + 12`.

This would give us a poetential encoding range of ``[12, 73]``, which is pretty good!
The slight quirk here is that we want to use the FLUSH symbol (14).
The symbol for (13) is shorter because it was intended to be the max pattern length, so all longer patterns get downmapped to it, making it more frequent.

We would want to do swap; here are a few potential C implementations that would need to be benchmarked for size and performance:

.. code-block:: c

    uint8_t swap_13_and_14(uint8_t val) {
        return (val > 12) ? (27 - val) : val;
    }

    uint8_t swap_13_14_xor(uint8_t value) {
        // value is in range [0, 14]
        uint8_t is_target = (value >=13);  // value MUST be 13 or 14; 15 does not exist.
        return value ^ (is_target | (is_target << 1));  // XOR with 0b11 to swap
    }

    uint8_t swap_13_14_lut(uint8_t value) {
        static const uint8_t lut[15] = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 13
        };
        return lut[val];
    }

Using this technique:

+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| dataset                       | raw         | tamp (max-pattern=15)  | tamp (rle + 6bit extension) | tamp (repurposed-huffman)  | Improvement (6bit to repurposed) |
+===============================+=============+========================+=============================+============================+==================================+
| enwik8                        | 100,000,000 | 51,635,633 (**1.937**) | 51,139,758 (**1.955**)      | 51,109,328 (**1.957**)     | **+0.060%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)  | 5,539,912 (**1.840**)       | 5,537,587 (**1.841**)      | **+0.042%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**) | 24,257,350 (**2.112**)      | 24,191,760 (**2.117**)     | **+0.270%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)  | 4,514,858 (**2.208**)       | 4,514,291 (**2.209**)      | **+0.013%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)  | 6,907,163 (**4.858**)       | 7,028,528 (**4.774**)      | :bad:`-1.757%`                   |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)  | 3,777,687 (**1.629**)       | 3,775,261 (**1.630**)      | **+0.064%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)  | 8,475,184 (**1.190**)       | 8,473,277 (**1.190**)      | **+0.023%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)  | 2,827,386 (**2.344**)       | 2,823,859 (**2.347**)      | **+0.125%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)  | 8,437,537 (**2.561**)       | 8,456,685 (**2.555**)      | :neutral:`-0.227%`               |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)  | 6,137,330 (**1.182**)       | 6,137,329 (**1.182**)      | **+0.000%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/webster               | 41,458,703  | 18,694,172 (**2.218**) | 18,224,796 (**2.275**)      | 18,212,452 (**2.276**)     | **+0.068%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)  | 7,510,986 (**1.128**)       | 7,510,986 (**1.128**)      | **+0.000%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)  | 1,496,263 (**3.572**)       | 1,502,674 (**3.557**)      | :neutral:`-0.428%`               |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+
| RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)    | 288,649 (**2.313**)         | 288,542 (**2.314**)        | **+0.037%**                      |
+-------------------------------+-------------+------------------------+-----------------------------+----------------------------+----------------------------------+

Unfortunately, it does not appear that this technique does not improve the results to justify the additional code/complexity.

We should also try this technique on the RLE encoding.

+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| dataset                       | raw         | tamp (max-pattern=15)    | tamp (RLE8 + 6bit match)    | tamp (Huffman RLE2 + 6bit match)  | tamp (Huffman RLE3 + 6bit match) | tamp (Huffman RLE4 + 6bit match) | Improvement (RLE8 -> Huffman RLE4) |
+===============================+=============+==========================+=============================+===================================+==================================+==================================+====================================+
| enwik8                        | 100,000,000 | 51,635,633 (**1.937**)   | 51,139,758 (**1.955**)      | 51,118,875 (**1.956**)            | 51,114,312 (**1.956**)           | 51,113,985 (**1.956**)           | **+0.050%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/dickens               | 10,192,446  | 5,546,761 (**1.838**)    | 5,539,912 (**1.840**)       | 5,539,855 (**1.840**)             | 5,539,839 (**1.840**)            | 5,539,834 (**1.840**)            | **+0.001%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/mozilla               | 51,220,480  | 25,121,385 (**2.039**)   | 24,257,350 (**2.112**)      | 24,300,139 (**2.108**)            | 24,260,320 (**2.111**)           | 24,241,409 (**2.113**)           | **+0.066%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/mr                    | 9,970,564   | 5,027,032 (**1.983**)    | 4,514,858 (**2.208**)       | 4,585,840 (**2.174**)             | 4,536,515 (**2.198**)            | 4,511,824 (**2.210**)            | **+0.067%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/nci                   | 33,553,445  | 8,643,610 (**3.882**)    | 6,907,163 (**4.858**)       | 6,907,097 (**4.858**)             | 6,898,504 (**4.864**)            | 6,898,462 (**4.864**)            | **+0.126%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/ooffice               | 6,152,192   | 3,814,938 (**1.613**)    | 3,777,687 (**1.629**)       | 3,777,230 (**1.629**)             | 3,775,473 (**1.630**)            | 3,774,855 (**1.630**)            | **+0.075%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/osdb                  | 10,085,684  | 8,520,835 (**1.184**)    | 8,475,184 (**1.190**)       | 8,470,768 (**1.191**)             | 8,469,489 (**1.191**)            | 8,469,489 (**1.191**)            | **+0.067%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/reymont               | 6,627,202   | 2,847,981 (**2.327**)    | 2,827,386 (**2.344**)       | 2,827,302 (**2.344**)             | 2,826,255 (**2.345**)            | 2,826,245 (**2.345**)            | **+0.040%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/samba                 | 21,606,400  | 9,102,594 (**2.374**)    | 8,437,537 (**2.561**)       | 8,460,650 (**2.554**)             | 8,435,732 (**2.561**)            | 8,425,496 (**2.564**)            | **+0.143%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/sao                   | 7,251,944   | 6,137,755 (**1.182**)    | 6,137,330 (**1.182**)       | 6,136,133 (**1.182**)             | 6,136,125 (**1.182**)            | 6,136,125 (**1.182**)            | **+0.020%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/webster               | 41,458,703  | 18,694,172 (**2.218**)   | 18,224,796 (**2.275**)      | 18,224,195 (**2.275**)            | 18,224,150 (**2.275**)           | 18,224,144 (**2.275**)           | **+0.004%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/x-ray                 | 8,474,240   | 7,510,606 (**1.128**)    | 7,510,986 (**1.128**)       | 7,508,571 (**1.129**)             | 7,508,169 (**1.129**)            | 7,508,169 (**1.129**)            | **+0.038%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| silesia/xml                   | 5,345,280   | 1,681,687 (**3.179**)    | 1,496,263 (**3.572**)       | 1,496,242 (**3.572**)             | 1,495,612 (**3.574**)            | 1,495,507 (**3.574**)            | **+0.051%**                        |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+
| RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 331,310 (**2.015**)      | 288,649 (**2.313**)         | 296,059 (**2.255**)               | 291,731 (**2.289**)              | 288,988 (**2.310**)              | :neutral:`-0.117%`                 |
+-------------------------------+-------------+--------------------------+-----------------------------+-----------------------------------+----------------------------------+----------------------------------+------------------------------------+

Overall, using 4 additional bits plus the existing huffman table slightly improved compression efficiency.

Another technique to reduce the skew of our huffman code is to truncate leading bits.
Our code makes it easy to truncate 1 or 2 bits to reduce the skew.
This has the side effect of making the huffman code dictionary smaller, limiting the upper limit of values that can be encoded this way.

For example:

* RLE4 can encode up to 240 bytes.
* RLE4T1 can encode up to 224 bytes.

+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| dataset                       | raw         | tamp (Huffman RLE4 + 6bit match) | tamp (Huffman RLE3T1 + 6bit match) | tamp (Huffman RLE4T1 + 6bit match) | Improvement (RLE4 -> RLE4T1) |
+===============================+=============+==================================+====================================+====================================+==============================+
| enwik8                        | 100,000,000 | 51,113,985 (**1.956**)           | 51,114,362 (**1.956**)             | 51,113,998 (**1.956**)             | :neutral:`-0.000%`           |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/dickens               | 10,192,446  | 5,539,834 (**1.840**)            | 5,539,840 (**1.840**)              | 5,539,835 (**1.840**)              | :neutral:`-0.000%`           |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/mozilla               | 51,220,480  | 24,241,409 (**2.113**)           | 24,260,538 (**2.111**)             | 24,241,930 (**2.113**)             | :neutral:`-0.002%`           |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/mr                    | 9,970,564   | 4,511,824 (**2.210**)            | 4,534,950 (**2.199**)              | 4,511,413 (**2.210**)              | **+0.009%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/nci                   | 33,553,445  | 6,898,462 (**4.864**)            | 6,898,530 (**4.864**)              | 6,898,467 (**4.864**)              | :neutral:`-0.000%`           |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/ooffice               | 6,152,192   | 3,774,855 (**1.630**)            | 3,775,597 (**1.629**)              | 3,774,869 (**1.630**)              | :neutral:`-0.000%`           |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/osdb                  | 10,085,684  | 8,469,489 (**1.191**)            | 8,469,489 (**1.191**)              | 8,469,489 (**1.191**)              | **+0.000%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/reymont               | 6,627,202   | 2,826,245 (**2.345**)            | 2,826,254 (**2.345**)              | 2,826,245 (**2.345**)              | **+0.000%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/samba                 | 21,606,400  | 8,425,496 (**2.564**)            | 8,434,941 (**2.562**)              | 8,425,164 (**2.565**)              | **+0.004%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/sao                   | 7,251,944   | 6,136,125 (**1.182**)            | 6,136,125 (**1.182**)              | 6,136,125 (**1.182**)              | **+0.000%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/webster               | 41,458,703  | 18,224,144 (**2.275**)           | 18,224,152 (**2.275**)             | 18,224,144 (**2.275**)             | **+0.000%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/x-ray                 | 8,474,240   | 7,508,169 (**1.129**)            | 7,508,169 (**1.129**)              | 7,508,169 (**1.129**)              | **+0.000%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| silesia/xml                   | 5,345,280   | 1,495,507 (**3.574**)            | 1,495,675 (**3.574**)              | 1,495,500 (**3.574**)              | **+0.000%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+
| RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 288,988 (**2.310**)              | 291,334 (**2.292**)                | 288,723 (**2.312**)                | **+0.092%**                  |
+-------------------------------+-------------+----------------------------------+------------------------------------+------------------------------------+------------------------------+

This optimization doesn't do much, and thusly will probably not be used.
Let's see if it makes any impact on extended match.

+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| dataset                       | raw         | tamp (Huffman RLE4 + 6bit match) | tamp (Huffman RLE4 + 2T1 match) | tamp (Huffman RLE4 + 2T2 match)  |
+===============================+=============+==================================+=================================+==================================+
| enwik8                        | 100,000,000 | 51,113,985 (**1.956**)           | 51,087,775                      | 51,100,117                       |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/dickens               | 10,192,446  | 5,539,834 (**1.840**)            | 5,537,723                       | 5,538,409                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/mozilla               | 51,220,480  | 24,241,409 (**2.113**)           | 24,173,987                      | 24,210,359                       |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/mr                    | 9,970,564   | 4,511,824 (**2.210**)            | 4,511,234                       | 4,511,387                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/nci                   | 33,553,445  | 6,898,462 (**4.864**)            | 7,016,461                       | 6,979,416                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/ooffice               | 6,152,192   | 3,774,855 (**1.630**)            | 3,772,789                       | 3,773,791                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/osdb                  | 10,085,684  | 8,469,489 (**1.191**)            | 8,469,411                       | 8,467,798                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/reymont               | 6,627,202   | 2,826,245 (**2.345**)            | 2,823,516                       | 2,823,419                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/samba                 | 21,606,400  | 8,425,496 (**2.564**)            | 8,443,582                       | 8,439,718                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/sao                   | 7,251,944   | 6,136,125 (**1.182**)            | 6,136,123                       | 6,136,124                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/webster               | 41,458,703  | 18,224,144 (**2.275**)           | 18,214,419                      | 18,208,504                       |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/x-ray                 | 8,474,240   | 7,508,169 (**1.129**)            | 7,508,169                       | 7,508,169                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| silesia/xml                   | 5,345,280   | 1,495,507 (**3.574**)            | 1,501,525                       | 1,502,539                        |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
| RPI_PICO-20250415-v1.25.0.uf2 | 667,648     | 288,988 (**2.310**)              | 288,719                         | 288,695                          |
+-------------------------------+-------------+----------------------------------+---------------------------------+----------------------------------+
