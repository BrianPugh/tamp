from io import BytesIO

try:
    from typing import Optional
except ImportError:
    pass

from . import compute_min_pattern_size, initialize_dictionary

_CHUNK_SIZE = 1 << 20
_FLUSH = object()

# Each key here are the huffman codes or'd with 0x80
# This is so that each lookup is easy/quick.
_huffman_lookup = {
    0b0: 0,
    0b11: 1,
    0b1000: 2,
    0b1011: 3,
    0b10100: 4,
    0b100100: 5,
    0b100110: 6,
    0b101011: 7,
    0b1001011: 8,
    0b1010100: 9,
    0b10010100: 10,
    0b10010101: 11,
    0b10101010: 12,
    0b100111: 13,
    0b10101011: _FLUSH,
}


class _BitReader:
    """Reads bits from a stream."""

    def __init__(self, f, close_f_on_close=False):
        self.close_f_on_close = close_f_on_close
        self.f = f
        self.clear()

    def read_huffman(self):
        proposed_code = 0
        lookup = _huffman_lookup
        read = self.read
        for _ in range(8):
            proposed_code |= read(1)
            try:
                return lookup[proposed_code]
            except KeyError:
                proposed_code <<= 1
        raise RuntimeError("Unable to decode huffman code. Should never happen.")

    def read(self, num_bits):
        while self.bit_pos < num_bits:
            byte = self.f.read(1)
            if not byte:
                raise EOFError
            byte_value = int.from_bytes(byte, "little")
            self.buffer |= byte_value << (24 - self.bit_pos)
            self.bit_pos += 8

            if self.backup_buffer is not None and self.backup_bit_pos is not None:
                self.backup_buffer |= byte_value << (24 - self.backup_bit_pos)
                self.backup_bit_pos += 8

        result = self.buffer >> (32 - num_bits)
        mask = (1 << (32 - num_bits)) - 1
        self.buffer = (self.buffer & mask) << num_bits
        self.bit_pos -= num_bits

        return result

    def clear(self):
        self.buffer = 0
        self.bit_pos = 0

        self.backup_buffer = None
        self.backup_bit_pos = None

    def close(self):
        if self.close_f_on_close:
            self.f.close()

    def __len__(self):
        return self.bit_pos

    def __enter__(self):
        # Context manager to restore all read bits in a session if any read fails.

        # backup the buffer
        self.backup_buffer = self.buffer
        self.backup_bit_pos = self.bit_pos
        return self

    def __exit__(self, exception_type, exception_value, exception_traceback):
        if exception_type is not None:
            # restore buffers
            self.buffer = self.backup_buffer
            self.bit_pos = self.backup_bit_pos

        self.backup_buffer = None
        self.backup_bit_pos = None


class _RingBuffer:
    def __init__(self, buffer):
        self.buffer = buffer
        self.size = len(buffer)
        self.pos = 0  # Always pointing to the byte-to-be-overwritten
        self.index = self.buffer.index

    def write_byte(self, byte):  # ~10% of time
        self.buffer[self.pos] = byte
        self.pos += 1
        if self.pos == self.size:
            self.pos = 0

    def write_bytes(self, data):
        for byte in data:
            self.write_byte(byte)


class Decompressor:
    """Decompresses a file or stream of tamp-compressed data.

    Can be used as a context manager to automatically handle file
    opening and closing:

    .. code-block:: python

        with tamp.Decompressor("compressed.tamp") as f:
            decompressed_data = f.read()
    """

    def __init__(self, f, *, dictionary: Optional[bytearray] = None):
        """
        Parameters
        ----------
        f: Union[file, str]
            File-like object to read compressed bytes from.
        dictionary: Optional[bytearray]
            Use the given **initialized** buffer inplace.
            At compression time, the same initialized buffer must be provided.
            Decompression stream's ``window`` must agree with the dictionary size.
            If providing a pre-allocated buffer, but with default initialization, it must
            first be initialized with :func:`~tamp.initialize_dictionary`
        """
        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")
            close_f_on_close = True
        else:
            close_f_on_close = False

        self._bit_reader = _BitReader(f, close_f_on_close=close_f_on_close)

        # Read Header
        self.window_bits = self._bit_reader.read(3) + 8
        self.literal_bits = self._bit_reader.read(2) + 5
        uses_custom_dictionary = self._bit_reader.read(1)
        reserved = self._bit_reader.read(1)
        more_header_bytes = self._bit_reader.read(1)

        if reserved:
            raise NotImplementedError

        if more_header_bytes:
            raise NotImplementedError

        if uses_custom_dictionary and dictionary is None:
            raise ValueError

        self._window_buffer = _RingBuffer(
            buffer=(dictionary if dictionary else initialize_dictionary(1 << self.window_bits)),
        )

        self.min_pattern_size = compute_min_pattern_size(self.window_bits, self.literal_bits)

        self.overflow = bytearray()

    def readinto(self, buf: bytearray) -> int:
        """Decompresses data into provided buffer.

        Parameters
        ----------
        buf: bytearray
            Buffer to decode data into.

        Returns
        -------
        int
            Number of bytes decompressed into buffer.
        """
        if len(self.overflow) > len(buf):
            buf[:] = self.overflow[: len(buf)]
            written = len(buf)
            self.overflow = self.overflow[len(buf) :]
            return written
        elif self.overflow:
            buf[: len(self.overflow)] = self.overflow
            written = len(self.overflow)
            self.overflow = bytearray()
        else:
            written = 0

        while written < len(buf):
            try:
                with self._bit_reader:
                    is_literal = self._bit_reader.read(1)

                    if is_literal:
                        c = self._bit_reader.read(self.literal_bits)
                        self._window_buffer.write_byte(c)
                        buf[written] = c
                        written += 1
                    else:
                        match_size = self._bit_reader.read_huffman()
                        if match_size is _FLUSH:
                            self._bit_reader.clear()
                            continue
                        match_size += self.min_pattern_size
                        index = self._bit_reader.read(self.window_bits)

                        string = self._window_buffer.buffer[index : index + match_size]
                        self._window_buffer.write_bytes(string)

                        to_buf = min(len(buf) - written, match_size)
                        buf[written : written + to_buf] = string[:to_buf]
                        written += to_buf
                        if to_buf < match_size:
                            self.overflow[:] = string[to_buf:]
                            break
            except EOFError:
                break

        return written

    def read(self, size: int = -1) -> bytearray:
        """Decompresses data to bytes.

        Parameters
        ----------
        size: int
            Maximum number of bytes to return.
            If a negative value is provided, all data will be returned.
            Defaults to ``-1``.

        Returns
        -------
        bytearray
            Decompressed data.
        """
        if size == 0:
            return bytearray()

        chunk_size = _CHUNK_SIZE
        out = []
        while True:
            buf = bytearray(chunk_size if size < 0 else size)
            chunk_size <<= 1  # Keep allocating larger chunks as we go on.
            read_size = self.readinto(buf)
            if size > 0:
                # Read the entire contents in one go.
                out.append(buf)
                break
            else:
                if read_size < len(buf):
                    if read_size:
                        out.append(buf[:read_size])
                    break
                else:
                    out.append(buf)
        return out[0] if len(out) == 1 else bytearray(b"".join(out))

    def close(self):
        """Closes the input file or stream, if tamp opened it."""
        self._bit_reader.close()

    def __enter__(self):
        """Use :class:`Decompressor` as a context manager.

        .. code-block:: python

           with tamp.Decompressor("output.tamp") as f:
               decompressed_data = f.read()
        """
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """Calls :meth:`~Decompressor.close` on contextmanager exit."""
        self.close()


class TextDecompressor(Decompressor):
    """Decompresses a file or stream of tamp-compressed data into text."""

    def read(self, size: int = -1) -> str:
        """Decompresses data to text.

        Parameters
        ----------
        size: int
            Maximum number of bytes to return.
            If a negative value is provided, all data will be returned.
            Defaults to ``-1``.

        Returns
        -------
        str
            Decompressed text.
        """
        return super().read(size).decode()


def decompress(data: bytes, *, dictionary: Optional[bytearray] = None) -> bytearray:
    """Single-call to decompress data.

    Parameters
    ----------
    data: bytes
        Tamp-compressed data to decompress.
    dictionary: Optional[bytearray]
        Use the given **initialized** buffer inplace.
        At compression time, the same initialized buffer must be provided.
        Decompression stream's ``window`` must agree with the dictionary size.
        If providing a pre-allocated buffer, but with default initialization, it must
        first be initialized with :func:`~tamp.initialize_dictionary`

    Returns
    -------
    bytearray
        Decompressed data.
    """
    with BytesIO(data) as f:
        d = Decompressor(f, dictionary=dictionary)
        return d.read()
