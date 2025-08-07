=================
Custom Dictionary
=================

Tamp is in the `LZ family <https://en.wikipedia.org/wiki/Dictionary_coder>`_ of compression algorithms.
Like other LZ-based compressors, Tamp builds a sliding window (AKA dictionary) of recently seen data to find repeating patterns.
For short messages, this window is relatively uninitialized, leading to relatively poor compression for the first few hundred bytes of a stream.

Tamp attempts to mitigate this by initializing this sliding window with a deterministic random combinations of letters (see :ref:`dictionary-initialization`).
The first bits of the dictionary look like this (where ``\x00`` is NULL; ``\n`` is newline):

.. code-block:: text

   \x00.//r.0. t>\n/>snas.trnr i\x00r/a\x00snat./.r\x00i o.s tneo>.as>\na.ta\x00 aa\x00\x00\x000oe ri\x00a>eatsi\n.\ni.str\n//snesr.ost

This "random" series of bytes contains many short patterns that are common in english text.

You, the developer, may have better insight on what the first few hundred bytes of your message may look like.
In these situations, having a shared custom dictionary can improve compression efficiency.
Custom dictionaries solve this problem by pre-populating the compression window with commonly occurring data patterns, allowing the algorithm to find matches immediately rather than having to "learn" patterns from recent input.

.. note::

   Custom dictionaries must be externally supplied to **both** the compressor and decompressor.
   The dictionary is not embedded in the compressed data, so both parties **must** have access to the same custom dictionary for successful compression and decompression.

When to Use Custom Dictionaries
================================

Custom dictionaries are most beneficial for:

* **Short predictable messages** - Where the sliding window doesn't have enough data to find patterns
* **Structured data** - JSON, XML, or other formats with repeating field names and structures
* **Domain-specific data** - Messages with known common prefixes, suffixes, or patterns
* **Protocol messages** - Network protocols or APIs with fixed headers and field names

If your use-case does not fit these situations, a custom dictionary may be more trouble than it is worth.

Improving JSON Compression
==========================

JSON is an ideal candidate for custom dictionaries because it contains many repeated elements:

* Field names that appear in every message
* Common values like ``true``, ``false``, ``null``
* Structural characters: ``{``, ``}``, ``[``, ``]``, ``:``
* Common string patterns

Basic Example
-------------

Consider this typical API response structure:

.. code-block:: json

   {
     "status": "success",
     "timestamp": "2024-01-15T10:30:00Z",
     "data": {
       "user_id": 12345,
       "username": "alice",
       "email": "alice@example.com",
       "is_active": true,
       "profile": {
         "first_name": "Alice",
         "last_name": "Smith",
         "created_at": "2023-06-01T09:00:00Z"
       }
     }
   }

We can create a custom dictionary containing all the common field names and values:

.. code-block:: python

    import tamp

    json_data = """\
    {
        "status": "success",
        "timestamp": "2024-01-15T10:30:00Z",
        "data": {
            "user_id": 12345,
            "username": "alice",
            "email": "alice@example.com",
            "is_active": true,
            "profile": {
            "first_name": "Alice",
            "last_name": "Smith",
            "created_at": "2023-06-01T09:00:00Z"
            }
        }
    }
    """

    # Initialize dictionary with Tamp's default algorithm
    window_bits = 10  # 1024 bytes
    dictionary = tamp.initialize_dictionary(1 << window_bits)

    # Add custom JSON patterns at the end (last to get overwritten)
    custom_patterns = b'{"status": "success","error": "timestamp": "data": "user_id": "username": "","email": "","is_active": truefalsenull,"profile": {"first_name": "","last_name": "","created_at": ""}}[]'
    dictionary[-len(custom_patterns) :] = custom_patterns

    # Compress with default initialization
    compressed_default = tamp.compress(json_data.encode("utf-8"))
    print(
        f"Default dictionary compressed ratio: {len(json_data) / len(compressed_default):.3}"
    )

    # Compress with custom dictionary
    compressed_custom = tamp.compress(json_data.encode("utf-8"), dictionary=dictionary)
    print(
        f"Custom dictionary compressed ratio: {len(json_data) / len(compressed_custom):.3}"
    )

    # Decompress (must use same dictionary)
    decompressed = tamp.decompress(compressed_custom, dictionary=dictionary)

Running this example demonstrates significantly higher compression ratios when using the custom dictionary for this short message:

.. code-block:: console

   $ python tamp-demo.py
   Default dictionary compressed ratio: 1.55
   Custom dictionary compressed ratio: 2.31

Performance Considerations
==========================

The effectiveness of a custom dictionary depends on:

* **Pattern frequency** - How often dictionary patterns appear **early** in your data
* **Dictionary size** - Larger dictionaries can hold more patterns but take up storage space
* **Message length** - Shorter messages (messages smaller than the window size) benefit most from custom dictionaries

Dictionary Initialization Strategy
----------------------------------

If your custom dictionary content is shorter than the sliding window size, it's recommended to initialize the dictionary buffer using Tamp's default initialization algorithm first, then append your custom patterns at the end.

This approach provides the best of both worlds:

* The default initialization fills the window with deterministic pseudo-random data that **might** provide some additional compression.
* Your custom patterns at the end are the last to get overwritten, so they are used for longer during compression.

**Python Example:**

.. code-block:: python

   import tamp

   # Get default initialized dictionary for window size
   window_bits = 10  # 1024 bytes
   dictionary = tamp.initialize_dictionary(1 << window_bits)

   # Append your custom patterns at the end
   custom_patterns = b'{"status":"success","error","timestamp":"data":'
   dictionary[-len(custom_patterns) :] = custom_patterns

   # Use the hybrid dictionary
   compressed = tamp.compress(data, dictionary=dictionary)

**JavaScript/TypeScript Example:**

.. code-block:: javascript

   import { initializeDictionary } from '@brianpugh/tamp';

   const windowBits = 10;
   const dictionarySize = 1 << windowBits;  // 1024 bytes

   // Initialize with Tamp's default algorithm
   const dictionary = await initializeDictionary(dictionarySize);

   // Add your custom patterns at the end (highest priority)
   const customPatterns = new TextEncoder().encode('{"status":"success","error","timestamp":"data":');
   const startIndex = dictionary.length - customPatterns.length;
   dictionary.set(customPatterns, startIndex);

   const options = { window: windowBits, dictionary: dictionary };
   const compressed = await compress(data, options);

Alternative Serialization Formats
---------------------------------

While custom dictionaries can improve compression significantly for short messages, it is important to also mention that more efficient initial representation of your data-to-be-compressed is also important.

For example, instead of transmitting JSON data, you may want to use something much more efficient like MessagePack.

MessagePack is a binary serialization format that's more compact than JSON:

.. code-block:: python

   import json
   import msgpack  # pip install msgpack
   import tamp

   data = {"user_id": 12345, "username": "alice", "is_active": True}

   # Original JSON approach
   json_data = json.dumps(data)
   json_compressed = tamp.compress(json_data)

   # MessagePack approach
   msgpack_data = msgpack.packb(data)
   msgpack_compressed = tamp.compress(msgpack_data)

   print(f"JSON size: {len(json_data)} -> {len(json_compressed)} bytes")
   print(f"MessagePack size: {len(msgpack_data)} -> {len(msgpack_compressed)} bytes")

.. code-block:: console

   $ python msgpack-demo.py
   JSON size: 58 -> 52 bytes
   MessagePack size: 38 -> 39 bytes  # Tamp was unable to compress msgpack; it actually made it worse!

Using an appropriate, equivalent custom dictionary for both serializations gives good results:

.. code-block:: python

   import json
   import msgpack
   import tamp

   data = {"user_id": 12345, "username": "alice", "is_active": True}

   window_bits = 10  # 1024 bytes
   dictionary = tamp.initialize_dictionary(1 << window_bits)

   # Original JSON approach
   json_data = json.dumps(data)
   json_custom_dictionary = dictionary.copy()
   json_patterns = b'{"user_id": 0, "username": ", "is_active": truefalse}'
   json_custom_dictionary[-len(json_patterns) :] = json_patterns
   json_compressed = tamp.compress(json_data, dictionary=json_custom_dictionary)

   # MessagePack approach
   msgpack_data = msgpack.packb(data)
   msgpack_custom_dictionary = dictionary.copy()
   msgpack_patterns = b"\x81\x82\x83\x84\xa7user_id\xa8username\xa9is_active\xc2\xc3"
   json_custom_dictionary[-len(msgpack_patterns) :] = msgpack_patterns
   msgpack_compressed = tamp.compress(msgpack_data, dictionary=json_custom_dictionary)

   print(f"JSON size: {len(json_data)} -> {len(json_compressed)} bytes")
   print(f"MessagePack size: {len(msgpack_data)} -> {len(msgpack_compressed)} bytes")

.. code-block:: console

   $ python msgpack-demo.py
   JSON size: 58 -> 22 bytes
   MessagePack size: 38 -> 17 bytes

With the gap narrowing, it is up to the developer to make an appropriate tradeoff between system complexity, data compression, and firmware size.
