from io import BytesIO

from . import compute_min_pattern_size, initialize_dictionary

_FLUSH = object()

# Each key here are the huffman codes or'd with 0x80
# This is so that each lookup is easy/quick.
huffman_lookup = {
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


class BitReader:
    """Reads bits from a stream."""

    def __init__(self, f, close_f_on_close=False):
        self.close_f_on_close = close_f_on_close
        self.f = f
        self.clear()

    def read_huffman(self):
        proposed_code = 0
        lookup = huffman_lookup
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
        self.f.close()
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


class RingBuffer:
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
    opening and closing::

        with tamp.Decompressor("compressed.tamp") as f:
            decompressed_data = f.read()
    """

    def __init__(self, f, *, dictionary=None):
        """
        Parameters
        ----------
        f: Union[file, str]
            File-like object to read compressed bytes from.
        dictionary: bytearray
            Use dictionary inplace as window buffer.
        """
        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")
            close_f_on_close = True
        else:
            close_f_on_close = False

        self._bit_reader = BitReader(f, close_f_on_close=close_f_on_close)

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

        if uses_custom_dictionary ^ bool(dictionary):
            raise ValueError

        self._window_buffer = RingBuffer(
            buffer=dictionary if dictionary else initialize_dictionary(1 << self.window_bits),
        )

        self.min_pattern_size = compute_min_pattern_size(self.window_bits, self.literal_bits)

        self.overflow = bytearray()

    def read(self, size=-1) -> bytearray:
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
        if size < 0:
            size = 0xFFFFFFFF

        if len(self.overflow) > size:
            out = self.overflow[:size]
            self.overflow = self.overflow[size:]
            return out
        elif self.overflow:
            out = self.overflow
            self.overflow = bytearray()
        else:
            out = bytearray()

        while len(out) < size:
            try:
                with self._bit_reader:
                    is_literal = self._bit_reader.read(1)

                    if is_literal:
                        c = self._bit_reader.read(self.literal_bits)
                        self._window_buffer.write_byte(c)
                        out.append(c)
                    else:
                        match_size = self._bit_reader.read_huffman()
                        if match_size is _FLUSH:
                            self._bit_reader.clear()
                            continue
                        match_size += self.min_pattern_size
                        index = self._bit_reader.read(self.window_bits)

                        string = self._window_buffer.buffer[index : index + match_size]
                        self._window_buffer.write_bytes(string)

                        out.extend(string)
                        if len(out) > size:
                            self.overflow[:] = out[size:]
                            out = out[:size]
                            break
            except EOFError:
                break

        return out

    def close(self):
        """Closes the input file or stream."""
        self._bit_reader.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextDecompressor(Decompressor):
    """Decompresses a file or stream of tamp-compressed data into text."""

    def read(self, *args, **kwargs) -> str:
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
        return super().read(*args, **kwargs).decode()


def decompress(data: bytes, *args, **kwargs) -> bytearray:
    """Single-call to decompress data.

    Parameters
    ----------
    data: bytes
        Plaintext data to compress.
    *args: tuple
        Passed along to :class:`Decompressor`.
    **kwargs : dict
        Passed along to :class:`Decompressor`.

    Returns
    -------
    bytearray
        Decompressed data.
    """
    with BytesIO(data) as f:
        d = Decompressor(f, *args, **kwargs)
        return d.read()
