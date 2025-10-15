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

try:
    import stringzilla as sz
except ImportError:
    sz = None

from . import ExcessBitsError, bit_size, compute_min_pattern_size, initialize_dictionary

# encodes [0, 14] pattern lengths
_huffman_codes = b"\x00\x03\x08\x0b\x14$&+KT\x94\x95\xaa'\xab"
# These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
_huffman_bits = b"\x02\x03\x05\x05\x06\x07\x07\x07\x08\x08\x09\x09\x09\x07\x09"
_FLUSH_CODE = 0xAB  # 8 bits
_RLE_SYMBOL = 12
_RLE_MAX_WINDOW = 8  # Maximum number of RLE bytes to write to the window.
_EXTENDED_MATCH_SYMBOL = 13
_LEADING_EXTENDED_MATCH_HUFFMAN_BITS = 3
_LEADING_RLE_HUFFMAN_BITS = 4


def _determine_rle_breakeven_point(min_pattern_size, window_bits):
    # Determines if a pattern-match would be shorter than a RLE match.
    # See how many bits this encoding would be with RLE
    rle_length_bits = {}
    for i in range(min_pattern_size, min_pattern_size + 11 + 1):
        rle_length_bits[i] = 8 + _LEADING_RLE_HUFFMAN_BITS + _huffman_bits[(i - 1) >> _LEADING_RLE_HUFFMAN_BITS]

    pattern_length_bits = {}
    for i in range(min_pattern_size, min_pattern_size + 11 + 1):
        pattern_length_bits[i] = _huffman_bits[i - min_pattern_size] + window_bits

    breakeven_point = 0
    for pattern_size in sorted(pattern_length_bits):
        if pattern_length_bits[pattern_size] < rle_length_bits[pattern_size]:
            breakeven_point = pattern_size

    return breakeven_point


class _BitWriter:
    """Writes bits to a stream."""

    def __init__(self, f, *, close_f_on_close: bool = False):
        self.close_f_on_close = close_f_on_close
        self.f = f
        self.buffer = 0  # Basically a uint32
        self.bit_pos = 0

    def write_huffman_and_literal_flag(self, pattern_size):
        # pattern_size in range [0, 14]
        return self.write(_huffman_codes[pattern_size], _huffman_bits[pattern_size])

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
        result = sz.find(self.buffer, pattern, start=start) if sz else self.buffer.find(pattern, start)

        if result == -1:
            raise ValueError("substring not found")
        return result

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

        # "+1" Because a RLE of 1 is not valid.
        self._rle_max_size = (13 << _LEADING_RLE_HUFFMAN_BITS) + (1 << _LEADING_RLE_HUFFMAN_BITS) + 1
        self._rle_breakeven = _determine_rle_breakeven_point(self.min_pattern_size, self.window_bits)

        self._extended_match_count = 0
        self._extended_match_position = 0

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
            self.max_pattern_size = (
                self.min_pattern_size
                + 11
                + (13 << _LEADING_EXTENDED_MATCH_HUFFMAN_BITS)
                + (1 << _LEADING_EXTENDED_MATCH_HUFFMAN_BITS)
            )
        else:
            self.max_pattern_size = self.min_pattern_size + 13

        self.literal_flag = 1 << self.literal_bits

        self._window_buffer = _RingBuffer(
            buffer=dictionary if dictionary else initialize_dictionary(1 << window),
        )

        self._input_buffer = deque(maxlen=16)  # matching the C implementation

        # Callbacks for debugging/metric collection; can be externally set.
        self.match_cb = None
        self.extended_match_cb = None
        self.literal_cb = None
        self.flush_cb = None
        self.rle_cb = None

        # For debugging: how many uncompressed bytes have we consumed so far.
        self.input_index = 0

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

        if self._extended_match_count:
            while self._input_buffer:
                if (self._extended_match_position + self._extended_match_count) >= self._window_buffer.size:
                    # wrap-around search: it's fine to check for the wrap now because it's super cheap here.
                    pos = (self._extended_match_position + self._extended_match_count) % self._window_buffer.size
                    if self._window_buffer.buffer[pos] == self._input_buffer[0]:
                        self._input_buffer.popleft()
                        self._extended_match_count += 1
                        if self._extended_match_count == self.max_pattern_size:
                            bytes_written += self._write_extended_match()
                            return bytes_written
                        continue
                    # We've found the end of the match
                    bytes_written += self._write_extended_match()
                    return bytes_written
                else:
                    # Search the remainder of the window buffer.
                    target = self._window_buffer.get(self._extended_match_position, self._extended_match_count)
                    target += bytes([self._input_buffer[0]])
                    search_i, match = self._search(target, start=self._extended_match_position)
                    match_size = len(match)
                    if match_size > self._extended_match_count:
                        self._input_buffer.popleft()
                        self._extended_match_count = match_size
                        self._extended_match_position = search_i
                        if self._extended_match_count == self.max_pattern_size:
                            bytes_written += self._write_extended_match()
                            return bytes_written
                        continue
                    else:
                        # We've found the end of the match
                        bytes_written += self._write_extended_match()
                        return bytes_written
            else:
                # We ran out of input_buffer, return so caller can re-populate the input_buffer
                return bytes_written

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
                    bytes_written += self._write_rle()
                    return bytes_written
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
            if match_size >= self._rle_count:
                # Pattern is better than RLE
                bytes_written += self._write_match(search_i, match)
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
                self._extended_match_position = search_i
                self._extended_match_count = match_size
            else:
                bytes_written += self._write_match(search_i, match)

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

    def _write_extended_huffman(self, value, leading_bits):
        bytes_written = 0
        # the upper bits can have values [0, 13]
        mask = (1 << leading_bits) - 1
        if value > ((13 << leading_bits) + mask) or value < 0:
            raise ValueError
        code_index = value >> leading_bits
        # Don't use write_huffman_and_literal_flag since we don't want to write a flag.
        bytes_written += self._bit_writer.write(_huffman_codes[code_index], _huffman_bits[code_index] - 1)
        bytes_written += self._bit_writer.write(value & mask, leading_bits)
        return bytes_written

    def _write_extended_match(self):
        bytes_written = 0
        if self.extended_match_cb:
            string = self._window_buffer.get(self._extended_match_position, self._extended_match_count)
            self.extended_match_cb(
                self._window_buffer.pos, self._extended_match_position, self._extended_match_count, string
            )
        bytes_written += self._bit_writer.write_huffman_and_literal_flag(_EXTENDED_MATCH_SYMBOL)
        bytes_written += self._bit_writer.write(self._extended_match_position, self.window_bits)
        bytes_written += self._write_extended_huffman(
            self._extended_match_count - self.min_pattern_size - 11 - 1,
            _LEADING_EXTENDED_MATCH_HUFFMAN_BITS,
        )

        self._window_buffer.write_from_self(self._extended_match_position, self._extended_match_count)

        # Reset state
        self._extended_match_count = 0
        self._extended_match_position = 0  # Technically not necessary.

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

    def _write_match(self, search_i, match) -> int:
        match_size = len(match)

        if self.match_cb:
            self.match_cb(
                self._window_buffer.pos,
                search_i,
                match_size,
                match,
            )

        bytes_written = 0
        bytes_written += self._bit_writer.write_huffman_and_literal_flag(match_size - self.min_pattern_size)
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
            bytes_written += self._write_literal(last_written_byte)
        else:
            if self.rle_cb:
                self.rle_cb(self._rle_count, last_written_byte)
            bytes_written += self._bit_writer.write_huffman_and_literal_flag(_RLE_SYMBOL)
            bytes_written += self._write_extended_huffman(self._rle_count - 2, _LEADING_RLE_HUFFMAN_BITS)

            if not self._rle_last_written:
                # Only write up to 8 bytes, and only if we didn't already do this.
                # This prevents filling up the window buffer with unhelpful data.
                self._window_buffer.write_bytes(bytes([last_written_byte]) * min(self._rle_count, _RLE_MAX_WINDOW))

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

        self.input_index = 0
        while self.input_index < len(data):
            if len(self._input_buffer) != self._input_buffer.maxlen:
                self._input_buffer.append(data[self.input_index])
                self.input_index += 1

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
