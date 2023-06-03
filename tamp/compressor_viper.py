"""Micropython optimized for performance over readability.
"""
from io import BytesIO

import micropython
from micropython import const

from . import ExcessBitsError, bit_size, compute_min_pattern_size, initialize_dictionary

# encodes [2, 15] pattern lengths
huffman_codes = b"\x00\x03\x08\x0b\x14$&+KT\x94\x95\xaa'"
# These bit lengths pre-add the 1 bit for the 0-value is_literal flag.
huffman_bits = b"\x02\x03\x05\x05\x06\x07\x07\x07\x08\x08\x09\x09\x09\x07"
FLUSH_CODE = const(0xAB)  # 8 bits


def _f_write(f, number, size):
    f.write(number.to_bytes(size, "big"))


class Compressor:
    def __init__(
        self,
        f,
        *,
        window=10,
        literal=8,
        dictionary=None,
    ):
        if not hasattr(f, "write"):  # It's probably a path-like object.
            f = open(str(f), "wb")
            self._close_f_on_close = True
        else:
            self._close_f_on_close = False

        self.window_bits = window
        self.literal_bits = literal

        self.min_pattern_size = compute_min_pattern_size(window, literal)
        self.max_pattern_size = self.min_pattern_size + 13
        self.max_pattern_bytes_exclusive = self.max_pattern_size + 1

        self.f = f
        self.f_buf = 0
        self.f_pos = 0

        # Window Buffer
        if dictionary:
            if bit_size(len(dictionary) - 1) != window:
                raise ValueError
            self.window_buf = dictionary
        else:
            self.window_buf = initialize_dictionary(1 << window)
        self.window_pos = 0

        # Input Buffer
        self.input_buf = bytearray(self.max_pattern_size)
        self.input_size = 0
        self.input_pos = 0

        # Write header
        self.f_buf = ((window - 8) << 5 | (literal - 5) << 3 | int(bool(dictionary)) << 2) << (22)
        self.f_pos = 8

    @micropython.viper
    def _compress_input_buffer_single(self) -> int:
        bytes_written = 0
        # Viper-ize everything
        input_buf = ptr8(self.input_buf)
        input_size = int(self.input_size)
        input_pos = int(self.input_pos)

        literal_bits = int(self.literal_bits)
        literal_flag = 1 << literal_bits

        f_buf = int(self.f_buf)
        f_pos = int(self.f_pos)
        huffman_bits_ptr8 = ptr8(huffman_bits)
        huffman_codes_ptr8 = ptr8(huffman_codes)

        window_bits = int(self.window_bits)
        window_buf = ptr8(self.window_buf)
        window_size = 1 << int(window_bits)
        window_pos = int(self.window_pos)

        min_pattern_size = int(self.min_pattern_size)
        max_pattern_size = int(self.max_pattern_size)

        f = self.f

        # Find largest match
        search_i = int(0)
        match_size = int(0)

        if input_size >= min_pattern_size:
            for window_index in range(window_size - min_pattern_size + 1):
                if input_buf[input_pos] != window_buf[window_index]:
                    continue  # Significant speed short-cut

                input_index = (input_pos + 1) % max_pattern_size
                if input_buf[input_index] != window_buf[window_index + 1]:
                    continue  # Small Speed short-cut

                current_match_size = int(2)
                for k in range(current_match_size, input_size):
                    input_index = (input_pos + k) % max_pattern_size
                    if input_buf[input_index] != window_buf[window_index + k]:
                        break
                    if window_index + k >= window_size:
                        break
                    current_match_size = k + 1
                if current_match_size > match_size:
                    match_size = current_match_size
                    search_i = window_index

                    if match_size == input_size:
                        break

        # Write out a literal or a token
        if match_size >= min_pattern_size:
            huffman_index = match_size - min_pattern_size
            # Adds up to 9 bits
            f_pos += huffman_bits_ptr8[huffman_index]
            f_buf |= huffman_codes_ptr8[huffman_index] << (30 - f_pos)

            if f_pos >= 16:
                _f_write(f, f_buf >> 14, 2)
                f_buf = (f_buf & 0x3FFF) << 16
                f_pos -= 16
                bytes_written += 2

            # Adds up to 15 bits
            f_pos += window_bits
            f_buf |= search_i << (30 - f_pos)
        else:
            # Adds up to 9 bits
            match_size = 1
            if input_buf[input_pos] >> literal_bits:
                raise ExcessBitsError

            f_pos += literal_bits + 1
            f_buf |= (input_buf[input_pos] | literal_flag) << (30 - f_pos)

        if f_pos >= 16:
            _f_write(f, f_buf >> 14, 2)
            f_buf = (f_buf & 0x3FFF) << 16
            f_pos -= 16
            bytes_written += 2

        for _ in range(match_size):  # Copy pattern into buffer
            window_buf[window_pos] = input_buf[input_pos]
            input_pos = (input_pos + 1) % max_pattern_size
            window_pos = (window_pos + 1) % window_size

        input_size -= match_size

        self.input_size = input_size
        self.input_pos = input_pos
        self.window_pos = window_pos

        self.f_pos = f_pos
        self.f_buf = f_buf

        return bytes_written

    @micropython.viper
    def write(self, data) -> int:
        bytes_written = 0

        data_l = int(len(data))
        data_p = ptr8(data)

        input_buf = ptr8(self.input_buf)

        max_pattern_size = int(self.max_pattern_size)

        for i in range(data_l):
            input_size = int(self.input_size)
            input_pos = int(self.input_pos)

            pos = (input_pos + input_size) % max_pattern_size
            input_buf[pos] = data_p[i]
            input_size += 1
            self.input_size = input_size

            if input_size == max_pattern_size:
                bytes_written += int(self._compress_input_buffer_single())
        return bytes_written

    def flush(self, write_token=True):
        bytes_written = 0
        while self.input_size:
            bytes_written += self._compress_input_buffer_single()

        if self.f_pos > 0 and write_token:
            self.f_pos += 9
            self.f_buf |= FLUSH_CODE << (30 - self.f_pos)

        while self.f_pos > 0:
            _f_write(self.f, self.f_buf >> 22, 1)
            self.f_buf = (self.f_buf & 0x03FFFFF) << 8
            self.f_pos -= 8
            bytes_written += 1

        self.f_pos = 0

        return bytes_written

    def close(self):
        bytes_written = 0
        bytes_written += self.flush(write_token=False)
        if self._close_f_on_close:
            self.f.close()
        return bytes_written

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextCompressor(Compressor):
    def write(self, data: str) -> int:
        return super().write(data.encode())


def compress(data, *args, **kwargs):
    with BytesIO() as f:
        cls = TextCompressor if isinstance(data, str) else Compressor
        c = cls(f, *args, **kwargs)
        c.write(data)
        c.flush(write_token=False)
        f.seek(0)
        return f.read()
