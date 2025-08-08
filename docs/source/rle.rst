===================
Run Length Encoding
===================
> [!NOTE]
> In this document, we will focus on the the typical Tamp configuration: `window=10`, `literal=8`.

A limitation of tamp's encoding system is that it can only handle relatively short patterns.
Under most configurations, tamp maxes out at 15 bytes.

For the typical configuration (`window=10`, `literal=8`), a 15-byte pattern takes ``1 + 6 + 10 = 17`` bits.
15 bytes is 120 bits, so ``120 / 17 = 7.0588`` should be the theoretical maximum compression ratio of data with Tamp.

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
TODO

Possible encoding schemes
-------------------------
Let's imagine our initial dictionary is all ``0x00``, and we wish to encode 1,000 bytes of `0xFF`.
How many writes do we need before we can take advantage of Tamp's full 15-bit pattern?

#. Write the ``0xFF`` literal (1 + 8 = 9 bits).
#. Write another ``0xFF`` literal (1 + 8 = 9 bits).
#. Write a 2-byte pattern match (1 + 1 + 10 = 12 bits).
#. Write a 4-byte pattern match (1 + 4 + 10 = 15 bits).
#. Write a 8-byte pattern match (1 + 6 + 10 = 17 bits).

This results in a total of 62 bits (7.75 bytes) until Tamp can be most efficient.
That means that the worst case (efficiency-wise) is attempting to encode 16 bytes.

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

But! For longer run-lengths, we could do even better!

What if we change the meaning of the "13" value to: "The next 8 bits after offset is the run-length"
That would be that we could still use the smaller values for smaller run-lengths, while still opening us up to very large run-lengths.
We could potentially encode 258 bytes in just 25 bits of data.
