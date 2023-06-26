from io import BytesIO

import micropython
from micropython import const

from . import compute_min_pattern_size, initialize_dictionary

_HUFFMAN_TABLE = b"BBBBBBBBBBBBBBBBeeee\x8a\x8bxxffffmmmmTTTTTTTTyy\x8c\x8fggggCCCCCCCCCCCCCCCC"

_FLUSH = const(15)


class Decompressor:
    def __init__(self, f, *, dictionary=None):
        self._close_f_on_close = False
        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")
            self._close_f_on_close = True

        self.f = f
        self.f_buf = 0
        self.f_pos = 0

        # Read Header
        header = self.f.read(1)[0]
        self.w_bits = (header >> 5) + 8
        self.literal_bits = ((header >> 3) & 0b11) + 5
        uses_custom_dictionary = header & 0b100
        reserved = header & 0b10
        more_header_bytes = header & 0b1

        if reserved or more_header_bytes:
            raise NotImplementedError

        if uses_custom_dictionary:
            if not dictionary:
                raise ValueError
            self.w_buf = dictionary
        else:
            self.w_buf = initialize_dictionary(1 << self.w_bits)
        self.w_pos = 0

        self.min_pattern_size = compute_min_pattern_size(self.w_bits, self.literal_bits)

        self.overflow_buf = bytearray(self.min_pattern_size + 13)
        self.overflow_pos = 0
        self.overflow_size = 0

    def readinto(self, buf):
        raise NotImplementedError

    @micropython.viper
    def _decompress_into(self, out) -> int:
        """Attempt to fill out, will place into overflow."""
        out_capacity = int(len(out))  # const
        out_buf = ptr8(out)

        huffman_table = ptr8(_HUFFMAN_TABLE)

        literal_bits = int(self.literal_bits)

        overflow_buf = self.overflow_buf
        overflow_buf_ptr = ptr8(overflow_buf)
        overflow_pos = int(self.overflow_pos)
        overflow_size = int(self.overflow_size)

        w_buf = self.w_buf
        w_buf_ptr = ptr8(w_buf)
        w_pos = int(self.w_pos)
        w_bits = int(self.w_bits)
        w_mask = (1 << w_bits) - 1

        f_pos = int(self.f_pos)
        f_buf = int(self.f_buf)
        f = self.f

        min_pattern_size = int(self.min_pattern_size)
        int(len(overflow_buf))

        # Copy overflow into out if available
        overflow_copy = int(min(overflow_size, out_capacity))
        for i in range(overflow_copy):
            out[i] = overflow_buf_ptr[overflow_pos + i]
        out_pos = overflow_copy
        overflow_pos += overflow_copy
        overflow_size -= overflow_copy

        full_mask = int(0x3FFFFFFF)

        # Decompress more
        try:
            while out_pos < out_capacity:
                overflow_pos = 0  # overflow_size is also always zero here

                if f_pos == 0:
                    f_buf = int(f.read(1)[0]) << 22  # Will raise IndexError if out of data.
                    f_pos = 8

                # re-use is_literal flag as match_size so we don't need to explicitly set it
                match_size = f_buf >> 29
                # Don't update f_buf & f_pos here, in case there's not enough data available.

                if match_size:
                    if f_pos < (literal_bits + 1):
                        f_buf |= int(f.read(1)[0]) << (22 - f_pos)
                        f_pos += 8

                    # Now update buffers from is_literal
                    f_buf = (f_buf << 1) & full_mask

                    c = f_buf >> (30 - literal_bits)
                    f_buf = (f_buf << literal_bits) & full_mask
                    f_pos -= literal_bits + 1

                    out_buf[out_pos] = c
                    w_buf_ptr[w_pos] = c
                    out_pos += 1
                else:
                    # Read and decode Huffman; we'll need the bits regardless
                    if f_pos < 9:
                        f_buf |= int(f.read(1)[0]) << (22 - f_pos)
                        f_pos += 8

                    if f_buf >> 28:
                        code = (f_buf >> 21) & 0x7F
                        code = 33 if code >= 64 else huffman_table[code]
                        match_size = code & 0xF
                        if match_size == _FLUSH:
                            f_buf = f_pos = 0
                            continue
                        delta = code >> 4
                    else:
                        match_size = 0
                        delta = 1

                    token_bits = 1 + delta + w_bits

                    while f_pos < token_bits:
                        f_buf |= int(f.read(1)[0]) << (22 - f_pos)
                        f_pos += 8

                    # We now absolutely have enough data to decode token.

                    match_size += min_pattern_size
                    index = (f_buf >> (30 - token_bits)) & w_mask
                    f_buf = (f_buf << token_bits) & full_mask
                    f_pos -= token_bits

                    # Copy entire match to overflow
                    for i in range(match_size):
                        overflow_buf_ptr[i] = w_buf_ptr[index + i]
                    overflow_pos = 0
                    overflow_size = match_size

                    # Copy available amount to out_buf
                    for i in range(match_size):
                        if out_pos < out_capacity:
                            out_buf[out_pos] = overflow_buf_ptr[overflow_pos]
                            out_pos += 1
                            overflow_pos += 1
                            overflow_size -= 1
                        # Copy overflow to window
                        w_buf_ptr[(w_pos + i) & w_mask] = overflow_buf_ptr[i]
                w_pos = (w_pos + match_size) & w_mask
        except IndexError:
            pass

        self.w_pos = w_pos
        self.overflow_pos = overflow_pos
        self.overflow_size = overflow_size
        self.f_buf = f_buf
        self.f_pos = f_pos

        return out_pos

    def read(self, size=-1):
        """Return at most ``size`` bytes."""
        if size == 0:
            out = bytearray()
        elif size < 0:
            # Read into a bunch of chunks, then do a single join.
            chunks = []
            chunk_size = 1024
            decompressed_bytes = 0
            while True:
                chunk = bytearray(chunk_size)
                chunk_decompressed_bytes = self._decompress_into(chunk)
                if chunk_decompressed_bytes != chunk_size:
                    chunk = chunk[:chunk_decompressed_bytes]
                chunks.append(chunk)

                if chunk_decompressed_bytes != chunk_size:
                    break

            out = b"".join(chunks)
        else:
            out = bytearray(size)
            decompressed_bytes = self._decompress_into(out)
            if decompressed_bytes != size:
                out = out[:decompressed_bytes]

        return out

    def close(self):
        if self._close_f_on_close:
            self.f.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()


class TextDecompressor(Decompressor):
    def read(self, *args, **kwargs) -> str:
        return super().read(*args, **kwargs).decode()


def decompress(data, *args, **kwargs):
    with BytesIO(data) as f:
        d = Decompressor(f, *args, **kwargs)
        return d.read()
