from collections import deque
from io import BytesIO

try:
    from typing import Optional, Union
except ImportError:
    pass

from . import ExcessBitsError, bit_size, compute_min_pattern_size, initialize_dictionary

# encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
_huffman_codes = b"\x00\x03\x08\x0b\x14$&+KT\x94\x95\xaa'"
# These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
_huffman_bits = b"\x02\x03\x05\x05\x06\x07\x07\x07\x08\x08\x09\x09\x09\x07"
_FLUSH_CODE = 0xAB  # 8 bits


class _BitWriter:
    """Writes bits to a stream."""

    def __init__(self, f, close_f_on_close=False):
        self.close_f_on_close = close_f_on_close
        self.f = f
        self.buffer = 0  # Basically a uint24
        self.bit_pos = 0

    def write_huffman(self, pattern_size):
        return self.write(_huffman_codes[pattern_size], _huffman_bits[pattern_size])

    def write(self, bits, num_bits, flush=True):
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

    def write_byte(self, byte):  # ~10% of time
        self.buffer[self.pos] = byte
        self.pos = (self.pos + 1) % self.size

    def write_bytes(self, data):
        for byte in data:
            self.write_byte(byte)

    def index(self, pattern, start):
        return self.buffer.index(pattern, start)


class Compressor:
    """Compresses data to a file or stream."""

    def __init__(
        self,
        f,
        *,
        window: int = 10,
        literal: int = 8,
        dictionary: Optional[bytearray] = None,
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
        """
        if not hasattr(f, "write"):  # It's probably a path-like object.
            # TODO: then close it on close
            f = open(str(f), "wb")
            close_f_on_close = True
        else:
            close_f_on_close = False

        self._bit_writer = _BitWriter(f, close_f_on_close=close_f_on_close)
        if dictionary and bit_size(len(dictionary) - 1) != window:
            raise ValueError("Dictionary-window size mismatch.")

        self.window_bits = window
        self.literal_bits = literal

        self.min_pattern_size = compute_min_pattern_size(window, literal)
        self.max_pattern_size = self.min_pattern_size + 13

        self.literal_flag = 1 << self.literal_bits

        self._window_buffer = _RingBuffer(
            buffer=dictionary if dictionary else initialize_dictionary(1 << window),
        )

        self._input_buffer = deque(maxlen=self.max_pattern_size)

        # Callbacks for debugging/metric collection; can be externally set.
        self.token_cb = None
        self.literal_cb = None
        self.flush_cb = None

        # Write header
        self._bit_writer.write(window - 8, 3, flush=False)
        self._bit_writer.write(literal - 5, 2, flush=False)
        self._bit_writer.write(bool(dictionary), 1, flush=False)
        self._bit_writer.write(0, 1, flush=False)  # Reserved
        self._bit_writer.write(0, 1, flush=False)  # No other header bytes

    def _compress_input_buffer_single(self) -> int:
        target = bytes(self._input_buffer)
        bytes_written = 0
        search_i = 0
        match_size = 1
        for match_size in range(self.min_pattern_size, len(target) + 1):
            match = target[:match_size]
            try:
                search_i = self._window_buffer.index(match, search_i)
            except ValueError:
                # Not Found
                match_size -= 1
                break
        match = target[:match_size]

        if match_size >= self.min_pattern_size:
            if self.token_cb:
                self.token_cb(
                    search_i,
                    match_size,
                    match,
                )
            bytes_written += self._bit_writer.write_huffman(match_size - self.min_pattern_size)
            bytes_written += self._bit_writer.write(search_i, self.window_bits)
            self._window_buffer.write_bytes(match)

            for _ in range(match_size):
                self._input_buffer.popleft()
        else:
            char = self._input_buffer.popleft()
            if self.literal_cb:
                self.literal_cb(char)
            if char >> self.literal_bits:
                raise ExcessBitsError

            bytes_written += self._bit_writer.write(char | self.literal_flag, self.literal_bits + 1)
            self._window_buffer.write_byte(char)

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
        bytes_written += self._bit_writer.flush(write_token=write_token)
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


    Returns
    -------
    bytes
        Compressed data
    """
    with BytesIO() as f:
        if isinstance(data, str):
            c = TextCompressor(f, window=window, literal=literal, dictionary=dictionary)
            c.write(data)
        else:
            c = Compressor(f, window=window, literal=literal, dictionary=dictionary)
            c.write(data)
        c.flush(write_token=False)
        f.seek(0)
        return f.read()
