"""Pure Python Tamp Compressor Reference Implementation.

The goal of this module is for clarity and to be able to easily test new ideas.
Do not optimize this file for speed, unless it still maintains clarity.

Some speed architectural optimizations might be tested here before implementing in other languages.
"""

from collections import deque
from io import BytesIO

try:
    from typing import Optional, Union
except ImportError:
    pass

from . import ExcessBitsError, bit_size, compute_min_pattern_size, initialize_dictionary

# encodes [0, 14] pattern lengths
_huffman_codes = b"\x00\x03\x08\x0b\x14$&+KT\x94\x95\xaa'\xab"
# These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
_huffman_bits = b"\x02\x03\x05\x05\x06\x07\x07\x07\x08\x08\x09\x09\x09\x07\x09"
_FLUSH_CODE = 0xAB  # 8 bits
_RLE_SYMBOL = 12
_RLE_BITS = 8  # MUST be 8 or less; there are design consequences otherwise.

_USE_RLE_EXTENDED_HUFFMAN = True
_RLE_EXTENDED_HUFFMAN_BITS = 4

_USE_MATCH_EXTENSION_HUFFMAN = False
_MATCH_EXTENSION_TRUNCATE_BITS = 0
if _MATCH_EXTENSION_TRUNCATE_BITS not in (0, 1, 2):
    raise ValueError

_MATCH_EXTENSION_BITS = 6
"""
if _USE_MATCH_EXTENSION_HUFFMAN
    _MATCH_EXTENSION_BITS represents the fixed number of MSb bits tacked on to huffman code.
else:
    _MATCH_EXTENSION_BITS represents the fixed number of bits.
"""

if not _USE_MATCH_EXTENSION_HUFFMAN and _MATCH_EXTENSION_TRUNCATE_BITS:
    raise ValueError


def _determine_rle_breakeven_point(min_pattern_size, window_bits):
    # See how many bits this encoding would be with RLE
    rle_length_bits = {}
    for i in range(min_pattern_size, min_pattern_size + 11 + 1):
        if _USE_RLE_EXTENDED_HUFFMAN:
            huffman_index = i >> _RLE_EXTENDED_HUFFMAN_BITS
            rle_length_bits[i] = 8 + _RLE_EXTENDED_HUFFMAN_BITS + _huffman_bits[huffman_index]
        else:
            rle_length_bits[i] = 8 + 8

    breakeven_point = 0
    for pattern_size in range(min_pattern_size, min_pattern_size + 11 + 1):
        huffman_index = pattern_size - min_pattern_size
        pattern_length_bits = _huffman_bits[huffman_index] + window_bits
        if pattern_length_bits < rle_length_bits[pattern_size]:
            breakeven_point = pattern_size
    return breakeven_point


class _BitWriter:
    """Writes bits to a stream."""

    def __init__(self, f, *, close_f_on_close: bool = False):
        self.close_f_on_close = close_f_on_close
        self.f = f
        self.buffer = 0  # Basically a uint32
        self.bit_pos = 0

    def write_huffman(self, pattern_size, truncate=0):
        # pattern_size in range [0, 14]
        return self.write(_huffman_codes[pattern_size], _huffman_bits[pattern_size])

    def write_extended_huffman(self, value, extended_bits, truncate_bits=0):
        if value >= ((15 - truncate_bits) * (1 << extended_bits)):
            raise ValueError

        bytes_written = 0
        huffman_value = value >> extended_bits

        huffman_value += truncate_bits

        # Swap 13 and 14 because 13 has a shorter huffman code.
        if huffman_value == 13:
            huffman_value = 14
        elif huffman_value == 14:
            huffman_value = 13

        if truncate_bits:
            code = _huffman_codes[huffman_value]
            code_length_bits = _huffman_bits[huffman_value] - truncate_bits
            self.write(code, code_length_bits)
        else:
            bytes_written += self.write_huffman(huffman_value)

        # TODO: should we write the 2 bits first or last? Does it matter?
        bytes_written += self.write(value & ((1 << extended_bits) - 1), 2)
        return bytes_written

    def write(self, bits, num_bits, flush=True):
        bits = int(bits)
        bits &= (1 << num_bits) - 1
        self.bit_pos += num_bits
        self.buffer |= bits << (32 - self.bit_pos)

        bytes_written = 0
        if flush:
            while self.bit_pos >= 8:
                byte = self.buffer >> 24
                self.f.write(byte.to_bytes(1, "big"))
                self.buffer = (self.buffer & 0xFFFFFF) << 8
                self.bit_pos -= 8
                bytes_written += 1
        return bytes_written

    def flush(self, write_token=True):
        bytes_written = 0
        if self.bit_pos > 0 and write_token:
            bytes_written += self.write(_FLUSH_CODE, 9)

        while self.bit_pos > 0:
            byte = (self.buffer >> 24) & 0xFF
            self.f.write(byte.to_bytes(1, "big"))
            self.bit_pos = 0
            self.buffer = 0
            bytes_written += 1

        self.f.flush()

        return bytes_written

    def close(self):
        self.flush(write_token=False)
        if self.close_f_on_close:
            self.f.close()


class _RingBuffer:
    def __init__(self, buffer):
        self.buffer = buffer
        self.size = len(buffer)
        self.pos = 0  # Always pointing to the byte-to-be-overwritten

    def write_byte(self, byte):
        self.buffer[self.pos] = byte
        self.pos = (self.pos + 1) % self.size

    def write_bytes(self, data):
        for byte in data:
            self.write_byte(byte)

    def index(self, pattern, start):
        return self.buffer.index(pattern, start)

    def write_from_self(self, position, size):
        data = [self.buffer[(position + i) % self.size] for i in range(size)]
        for x in data:
            self.write_byte(x)

    def get(self, index, size):
        out = bytearray(size)
        for i in range(size):
            pos = (index + i) % self.size
            out[i] = self.buffer[pos]
        return bytes(out)

    @property
    def last_written_byte(self) -> int:
        pos = self.pos - 1
        if pos < 0:
            pos = self.size - 1
        return self.buffer[pos]  # TODO: unit-test this thoroughly on initial start!


class Compressor:
    """Compresses data to a file or stream."""

    def __init__(
        self,
        f,
        *,
        window: int = 10,
        literal: int = 8,
        dictionary: Optional[bytearray] = None,
        lazy_matching: bool = False,
        v2: bool = True,
    ):
        """
        Parameters
        ----------
        f: Union[str, Path, FileLike]
            Path/FileHandle/Stream to write compressed data to.
        window: int
            Size of window buffer in bits.
            Higher values will typically result in higher compression ratios and higher computation cost.
            A same size buffer is required at decompression time.
            Valid range: ``[8, 15]``.
            Defaults to ``10`` (``1024`` byte buffer).
        literal: int
            Number of used bits in each byte of data.
            The default ``8`` bits can store all data.
            A common other value is ``7`` for storing ascii characters where the most-significant-bit is always 0.
            Smaller values result in higher compression ratios for no additional computation cost.
            Valid range: ``[5, 8]``.
        dictionary: Optional[bytearray]
            Use the given **initialized** buffer inplace.
            At decompression time, the same initialized buffer must be provided.
            ``window`` must agree with the dictionary size.
            If providing a pre-allocated buffer, but with default initialization, it must
            first be initialized with :func:`~tamp.initialize_dictionary`
        lazy_matching: bool
            Use roughly 50% more cpu to get 0~2% better compression.
        """
        self.window_bits = window
        self.literal_bits = literal
        self.min_pattern_size = compute_min_pattern_size(window, literal)
        self.v2: bool = v2
        self._rle_count = 0
        self._rle_last_written = False  # The previous write was an RLE token

        if _USE_RLE_EXTENDED_HUFFMAN:
            self._rle_max_size = 15 * (1 << _RLE_EXTENDED_HUFFMAN_BITS)
        else:
            self._rle_max_size = 1 << _RLE_BITS
        self._rle_breakeven = _determine_rle_breakeven_point(self.min_pattern_size, self.window_bits)

        self._extended_pattern_match_count = 0
        self._extended_pattern_match_position = 0
        # We could write the first 1 + 6 + 15 bits while waiting for the final 6 bits, making it safe!

        self.lazy_matching = lazy_matching
        self._cached_match_index = -1
        self._cached_match_size = 0

        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = open(str(f), "wb")
            close_f_on_close = True
        else:
            close_f_on_close = False

        self._bit_writer = _BitWriter(f, close_f_on_close=close_f_on_close)
        if dictionary and bit_size(len(dictionary) - 1) != window:
            raise ValueError("Dictionary-window size mismatch.")

        if self.v2:
            if _USE_MATCH_EXTENSION_HUFFMAN:
                self.max_pattern_size = (
                    self.min_pattern_size + 11 + (15 - _MATCH_EXTENSION_TRUNCATE_BITS) * (1 << _MATCH_EXTENSION_BITS)
                )
            else:
                self.max_pattern_size = self.min_pattern_size + 11 + (1 << _MATCH_EXTENSION_BITS)
        else:
            self.max_pattern_size = self.min_pattern_size + 13

        self.literal_flag = 1 << self.literal_bits

        self._window_buffer = _RingBuffer(
            buffer=dictionary if dictionary else initialize_dictionary(1 << window),
        )

        self._input_buffer = deque(maxlen=16)  # matching the C implementation

        # Callbacks for debugging/metric collection; can be externally set.
        self.token_cb = None
        self.literal_cb = None
        self.flush_cb = None
        self.rle_cb = None

        # Write header
        self._bit_writer.write(window - 8, 3, flush=False)
        self._bit_writer.write(literal - 5, 2, flush=False)
        self._bit_writer.write(bool(dictionary), 1, flush=False)
        self._bit_writer.write(self.v2, 1, flush=False)
        self._bit_writer.write(0, 1, flush=False)  # No other header bytes

    def _validate_no_match_overlap(self, write_pos, match_index, match_size):
        """Check if writing a single byte will overlap with a future match section."""
        return write_pos < match_index or write_pos >= match_index + match_size

    def _compress_input_buffer_single(self) -> int:
        bytes_written = 0

        if not self._input_buffer:
            return bytes_written

        if self._extended_pattern_match_count:
            target = self._window_buffer.get(self._extended_pattern_match_position, self._extended_pattern_match_count)
            while self._input_buffer:
                target += bytes([self._input_buffer[0]])
                search_i, match = self._search(target, start=self._extended_pattern_match_position)
                match_size = len(match)
                if match_size > self._extended_pattern_match_count:
                    self._input_buffer.popleft()
                    self._extended_pattern_match_count = match_size
                    self._extended_pattern_match_position = search_i
                    if self._extended_pattern_match_count == self.max_pattern_size:
                        bytes_written += self._write_pattern_extended_bits()
                        return bytes_written
                    continue
                elif search_i + match_size >= self._window_buffer.size:
                    # wrap-around search: it's fine to check for the wrap now because it's super cheap here.
                    while self._input_buffer:
                        pos = (search_i + match_size) % self._window_buffer.size
                        if self._window_buffer.buffer[pos] == self._input_buffer[0]:
                            self._input_buffer.popleft()
                            self._extended_pattern_match_count += 1
                            if self._extended_pattern_match_count == self.max_pattern_size:
                                bytes_written += self._write_pattern_extended_bits()
                                return bytes_written
                            continue
                        # We've found the end of the match
                        break
                    else:
                        # We ran out of input_buffer, return so caller can re-populate the input_buffer
                        return bytes_written

                # We've found the end of the match
                bytes_written += self._write_pattern_extended_bits()
                return bytes_written
            else:
                # We ran out of input_buffer, return so caller can re-populate the input_buffer
                return bytes_written

            raise NotImplementedError("Unreachable")

        target = bytes(self._input_buffer)
        search_i = 0
        match_size = 1

        if self.v2:
            # RLE same-character-counting logic
            while (
                target and target[0] == self._window_buffer.last_written_byte and self._rle_count < self._rle_max_size
            ):
                self._rle_count += 1
                self._input_buffer.popleft()
                target = bytes(self._input_buffer)
            if not target and self._rle_count != self._rle_max_size:
                # Need more input to see if the RLE continues
                return bytes_written
            if self._rle_count == 1:
                # This is not RLE; attempt to pattern-match or just write literals.
                self._input_buffer.appendleft(self._window_buffer.last_written_byte)
                target = bytes(self._input_buffer)
                self._rle_count = 0
            elif self._rle_count:
                if self._rle_count > self._rle_breakeven:
                    # It's certainly better to do a RLE write than searching for a pattern.
                    return self._write_rle()
                else:
                    # We'll see if pattern-matching offers a better encoding.
                    target = bytes([self._window_buffer.last_written_byte]) * self._rle_count

        # Check if we have a cached match from lazy matching
        if self.lazy_matching and self._cached_match_index >= 0:
            search_i = self._cached_match_index
            match_size = self._cached_match_size
            match = self._window_buffer.get(search_i, match_size)
            self._cached_match_index = -1  # Clear cache after using
        else:
            # Perform normal pattern-matching
            search_i, match = self._search(target, start=0)
            match_size = len(match)

        if self._rle_count:
            # Check to see if the found pattern-match is more efficient than the RLE encoding.
            assert self._rle_count >= 2  # noqa: S101
            if match_size == self._rle_count:
                # Pattern is better than RLE
                bytes_written += self._write_pattern(search_i, match)
                self._rle_count = 0
                return bytes_written
            else:
                # RLE is better than pattern
                return self._write_rle()

        # Lazy matching logic
        if (
            self.lazy_matching
            and match_size >= self.min_pattern_size
            and match_size <= 8
            and len(self._input_buffer) > match_size + 2
        ):
            # Check if next position has a better match
            next_target = bytes(list(self._input_buffer)[1:])  # Skip first byte
            next_search_i, next_match = self._search(next_target, start=0)
            next_match_size = len(next_match)

            # If next position has a better match, and the match doesn't overlap with the literal we are writing
            if next_match_size > match_size and self._validate_no_match_overlap(
                self._window_buffer.pos, next_search_i, next_match_size
            ):
                # Write literal at current position and cache the next match
                literal = self._input_buffer.popleft()
                bytes_written += self._write_literal(literal)
                self._cached_match_index = next_search_i
                self._cached_match_size = next_match_size
                return bytes_written

        if match_size >= self.min_pattern_size:
            if self.v2 and match_size > (self.min_pattern_size + 11):
                # Protects +12 to be RLE symbol, and +13 to be extended match symbol
                # Write the "extended" match symbol
                bytes_written += self._bit_writer.write_huffman(13)
                bytes_written += self._bit_writer.write(search_i, self.window_bits)
                self._extended_pattern_match_position = search_i
                self._extended_pattern_match_count = match_size
            else:
                bytes_written += self._write_pattern(search_i, match)

            self._rle_last_written = False
            for _ in range(match_size):
                self._input_buffer.popleft()
        else:
            literal = self._input_buffer.popleft()
            bytes_written += self._write_literal(literal)

        return bytes_written

    def _search(self, target: bytes, start=0):
        match_size = 0
        search_i = start
        for match_size in range(
            self.min_pattern_size,
            min(len(target), self.max_pattern_size) + 1,
        ):
            match = target[:match_size]
            try:
                search_i = self._window_buffer.index(match, search_i)
            except ValueError:
                # Not Found
                match_size -= 1
                break
        match = target[:match_size]
        return search_i, match

    def _write_pattern_extended_bits(self):
        bytes_written = 0
        # print(self._extended_pattern_match_count)
        # breakpoint()
        if _USE_MATCH_EXTENSION_HUFFMAN:
            bytes_written += self._bit_writer.write_extended_huffman(
                self._extended_pattern_match_count - self.min_pattern_size - 11 - 1,
                _MATCH_EXTENSION_BITS,
                truncate_bits=_MATCH_EXTENSION_TRUNCATE_BITS,
            )
        else:
            bytes_written += self._bit_writer.write(
                self._extended_pattern_match_count - self.min_pattern_size - 11 - 1,
                _MATCH_EXTENSION_BITS,
            )

        self._window_buffer.write_from_self(self._extended_pattern_match_position, self._extended_pattern_match_count)

        # Reset state
        self._extended_pattern_match_count = 0
        self._extended_pattern_match_position = 0  # Technically not necessary.

        return bytes_written

    def _write_literal(self, literal) -> int:
        bytes_written = 0
        if self.literal_cb:
            self.literal_cb(literal)
        if literal >> self.literal_bits:
            raise ExcessBitsError

        bytes_written += self._bit_writer.write(literal | self.literal_flag, self.literal_bits + 1)
        self._window_buffer.write_byte(literal)
        self._rle_last_written = False
        return bytes_written

    def _write_pattern(self, search_i, match) -> int:
        match_size = len(match)

        if self.token_cb:
            self.token_cb(
                search_i,
                match_size,
                match,
            )

        bytes_written = 0
        bytes_written += self._bit_writer.write_huffman(match_size - self.min_pattern_size)
        bytes_written += self._bit_writer.write(search_i, self.window_bits)
        self._window_buffer.write_bytes(match)
        self._rle_last_written = False
        return bytes_written

    def _write_rle(self) -> int:
        bytes_written = 0
        last_written_byte = self._window_buffer.last_written_byte

        if self._rle_count == 0:
            raise ValueError("No RLE to write.")
        elif self._rle_count == 1:
            # Just write a literal
            bytes_written += self._bit_writer.write(last_written_byte | self.literal_flag, self.literal_bits + 1)
        else:
            if self.rle_cb:
                self.rle_cb(self.rle_cb(self._rle_count, last_written_byte))
            bytes_written += self._bit_writer.write_huffman(_RLE_SYMBOL)
            if _USE_RLE_EXTENDED_HUFFMAN:
                bytes_written += self._bit_writer.write_extended_huffman(
                    self._rle_count - 1, _RLE_EXTENDED_HUFFMAN_BITS
                )
            else:
                bytes_written += self._bit_writer.write(self._rle_count - 1, _RLE_BITS)

            if not self._rle_last_written:
                # Only write up to 8 bytes, and only if we didn't already do this.
                # This prevents filling up the window buffer with unhelpful data.
                self._window_buffer.write_bytes(bytes([last_written_byte]) * min(self._rle_count, 8))

            self._rle_last_written = True

        self._rle_count = 0
        return bytes_written

    def write(self, data: Union[bytes, bytearray]) -> int:
        """Compress ``data`` to stream.

        Parameters
        ----------
        data: Union[bytes, bytearray]
            Data to be compressed.

        Returns
        -------
        int
            Number of compressed bytes written.
            May be zero when data is filling up internal buffers.
        """
        bytes_written = 0

        for char in data:
            self._input_buffer.append(char)
            if len(self._input_buffer) == self._input_buffer.maxlen:
                bytes_written += self._compress_input_buffer_single()

        return bytes_written

    def flush(self, write_token: bool = True) -> int:
        """Flushes all internal buffers.

        This compresses any data remaining in the input buffer,
        and flushes any remaining data in the output buffer to
        disk.

        Parameters
        ----------
        write_token: bool
            If appropriate, write a ``FLUSH`` token.
            Defaults to :obj:`True`.

        Returns
        -------
        int
            Number of compressed bytes flushed to disk.
        """
        bytes_written = 0
        if self.flush_cb:
            self.flush_cb()
        while self._input_buffer:
            bytes_written += self._compress_input_buffer_single()
        if self.v2 and self._rle_count:
            bytes_written += self._write_rle()

        # Clear any cached lazy matching state
        if self.lazy_matching:
            self._cached_match_index = -1
            self._cached_match_size = 0

        bytes_written_flush = self._bit_writer.flush(write_token=write_token)
        bytes_written += bytes_written_flush
        if bytes_written_flush:
            self._rle_last_written = False
        return bytes_written

    def close(self) -> int:
        """Flushes all internal buffers and closes the output file or stream, if tamp opened it.

        Returns
        -------
        int
            Number of compressed bytes flushed to disk.
        """
        bytes_written = 0
        bytes_written += self.flush(write_token=False)
        self._bit_writer.close()
        return bytes_written

    def __enter__(self) -> "Compressor":
        """Use :class:`Compressor` as a context manager.

        .. code-block:: python

           with tamp.Compressor("output.tamp") as f:
               f.write(b"foo")
        """
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Calls :meth:`~Compressor.close` on contextmanager exit."""
        self.close()


class TextCompressor(Compressor):
    """Compresses text to a file or stream."""

    def write(self, data: str) -> int:
        return super().write(data.encode())


def compress(
    data: Union[bytes, str],
    *,
    window: int = 10,
    literal: int = 8,
    dictionary: Optional[bytearray] = None,
    lazy_matching: bool = False,
    v2: bool = True,
) -> bytes:
    """Single-call to compress data.

    Parameters
    ----------
    data: Union[str, bytes]
        Data to compress.
    window: int
        Size of window buffer in bits.
        Higher values will typically result in higher compression ratios and higher computation cost.
        A same size buffer is required at decompression time.
        Valid range: ``[8, 15]``.
        Defaults to ``10`` (``1024`` byte buffer).
    literal: int
        Number of used bits in each byte of data.
        The default ``8`` bits can store all data.
        A common other value is ``7`` for storing ascii characters where the most-significant-bit is always 0.
        Valid range: ``[5, 8]``.
    dictionary: Optional[bytearray]
        Use the given **initialized** buffer inplace.
        At decompression time, the same initialized buffer must be provided.
        ``window`` must agree with the dictionary size.
        If providing a pre-allocated buffer, but with default initialization, it must
        first be initialized with :func:`~tamp.initialize_dictionary`
    lazy_matching: bool
        Use roughly 50% more cpu to get 0~2% better compression.
    v2: bool
        Use v2 compression format. Defaults to True.

    Returns
    -------
    bytes
        Compressed data
    """
    with BytesIO() as f:
        if isinstance(data, str):
            c = TextCompressor(
                f,
                window=window,
                literal=literal,
                dictionary=dictionary,
                lazy_matching=lazy_matching,
                v2=v2,
            )
            c.write(data)
        else:
            c = Compressor(
                f,
                window=window,
                literal=literal,
                dictionary=dictionary,
                lazy_matching=lazy_matching,
                v2=v2,
            )
            c.write(data)
        c.flush(write_token=False)
        f.seek(0)
        return f.read()
