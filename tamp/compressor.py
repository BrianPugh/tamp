from collections import deque
from io import BytesIO

try:
    from typing import Union
except ImportError:
    pass

from . import ExcessBitsError, bit_size, compute_min_pattern_size, initialize_dictionary

try:
    from micropython import const
except ImportError:

    def const(x):
        return x  # noqa: E721


# encodes [min_pattern_bytes, min_pattern_bytes + 13] pattern lengths
huffman_codes = b"\x00\x03\x08\x0b\x14$&+KT\x94\x95\xaa'"
# These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
huffman_bits = b"\x02\x03\x05\x05\x06\x07\x07\x07\x08\x08\x09\x09\x09\x07"
FLUSH_CODE = const(0xAB)  # 8 bits


class BitWriter:
    """Writes bits to a stream."""

    def __init__(self, f, close_f_on_close=False):
        self.close_f_on_close = close_f_on_close
        self.f = f
        self.buffer = 0  # Basically a uint24
        self.bit_pos = 0

    def write_huffman(self, pattern_size):
        return self.write(huffman_codes[pattern_size], huffman_bits[pattern_size])

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
            bytes_written += self.write(FLUSH_CODE, 9)

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


class RingBuffer:
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
        window=10,
        literal=8,
        dictionary=None,
    ):
        """
        Parameters
        ----------
        window: int
            Size of window buffer in bits.
            Defaults to 10 (1024 byte buffer).
        literal: int
            Size of literals in bits.
            Defaults to 8.
        dictionary: Optional[bytearray]
            Use the given initialized buffer inplace.
            At decompression time, the same buffer must be provided.
            ``window`` must agree with the dictionary size.
        """
        if not hasattr(f, "write"):  # It's probably a path-like object.
            # TODO: then close it on close
            f = open(str(f), "wb")
            close_f_on_close = True
        else:
            close_f_on_close = False

        self._bit_writer = BitWriter(f, close_f_on_close=close_f_on_close)
        if dictionary and bit_size(len(dictionary) - 1) != window:
            raise ValueError("Dictionary-window size mismatch.")

        self.window_bits = window
        self.literal_bits = literal

        self.min_pattern_size = compute_min_pattern_size(window, literal)
        self.max_pattern_size = self.min_pattern_size + 13

        self.literal_flag = 1 << self.literal_bits

        self._window_buffer = RingBuffer(
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

    def write(self, data: bytes) -> int:
        """Compress ``data`` to stream.

        Parameters
        ----------
        data: bytes
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
        """Flushes internal buffers.

        Parameters
        ----------
        write_token: bool
            If appropriate, write a ``FLUSH`` token.
            Defaults to ``True``.

        Returns
        -------
        int
            Number of compressed bytes flushed.
        """
        bytes_written = 0
        if self.flush_cb:
            self.flush_cb()
        while self._input_buffer:
            bytes_written += self._compress_input_buffer_single()
        bytes_written += self._bit_writer.flush(write_token=write_token)
        return bytes_written

    def close(self) -> int:
        """Flushes internal buffers and close the output file or stream.

        Returns
        -------
        int
            Number of compressed bytes flushed.
        """
        bytes_written = 0
        bytes_written += self.flush(write_token=False)
        self._bit_writer.close()
        return bytes_written

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextCompressor(Compressor):
    """Compresses text to a file or stream."""

    def write(self, data: str) -> int:
        return super().write(data.encode())


def compress(data: Union[bytes, str], *args, **kwargs) -> bytes:
    """Single-call to compress data.

    Parameters
    ----------
    data: Union[str, bytes]
        Data to compress.
    *args: tuple
        Passed along to :class:`Compressor`.
    **kwargs : dict
        Passed along to :class:`Compressor`.

    Returns
    -------
    bytes
        Compressed data
    """
    with BytesIO() as f:
        if isinstance(data, str):
            c = TextCompressor(f, *args, **kwargs)
            c.write(data)
        else:
            c = Compressor(f, *args, **kwargs)
            c.write(data)
        c.flush(write_token=False)
        f.seek(0)
        return f.read()
