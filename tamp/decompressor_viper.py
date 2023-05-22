from io import BytesIO

import micropython

from . import compute_min_pattern_size, initialize_dictionary


class Decompressor:
    def __init__(self, f, *, dictionary=None):
        if not hasattr(f, "read"):  # It's probably a path-like object.
            f = open(str(f), "rb")

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

        if reserved:
            raise NotImplementedError

        if more_header_bytes:
            raise NotImplementedError

        if uses_custom_dictionary:
            if not dictionary:
                raise ValueError
            self.w_buf = dictionary
        else:
            self.w_buf = initialize_dictionary(1 << self.w_bits)
        self.w_pos = 0

        self.min_pattern_size = compute_min_pattern_size(self.w_bits, self.literal_bits)
        self.max_pattern_size = self.min_pattern_size + 13

        self.overflow_buf = bytearray(self.min_pattern_size + 13)
        self.overflow_pos = 0
        self.overflow_size = 0

    def readinto(self, buf):
        raise NotImplementedError

    @micropython.viper
    def _decompress_into(self, out) -> int:
        """Attempt to fill out, will place into overflow."""
        out_pos = 0
        out_capacity = int(len(out))  # const
        out_buf = ptr8(out)

        literal_bits = int(self.literal_bits)

        overflow_buf = ptr8(self.overflow_buf)
        overflow_pos = int(self.overflow_pos)
        overflow_size = int(self.overflow_size)

        w_buf = ptr8(self.w_buf)
        w_pos = int(self.w_pos)
        w_bits = int(self.w_bits)
        w_mask = (1 << w_bits) - 1

        f_pos = int(self.f_pos)
        f_buf = int(self.f_buf)
        f = self.f

        min_pattern_size = int(self.min_pattern_size)
        max_pattern_size = int(len(self.overflow_buf))

        # Copy overflow into out
        while overflow_size and out_pos < out_capacity:
            out_buf[out_pos] = overflow_buf[overflow_pos]
            out_pos += 1
            overflow_pos = (overflow_pos + 1) % max_pattern_size
            overflow_size -= 1

        full_mask = int(0x3FFFFFFF)

        # Decompress more
        try:
            while out_pos < out_capacity:
                if f_pos == 0:
                    f_buf = int(f.read(1)[0]) << 22
                    f_pos = 8

                # re-use is_literal flag as match_size so we don't need to explicitly set it
                match_size = f_buf >> 29
                f_buf = (f_buf << 1) & full_mask
                f_pos -= 1

                if match_size:
                    if f_pos < literal_bits:
                        f_buf |= int(f.read(1)[0]) << (22 - f_pos)
                        f_pos += 8
                    c = f_buf >> (30 - literal_bits)
                    f_pos -= literal_bits
                    f_buf = (f_buf << literal_bits) & full_mask

                    out_buf[out_pos] = c
                    w_buf[w_pos] = c
                else:
                    # Read and decode Huffman
                    # always read; we'll need the bits regardless
                    f_buf |= int(f.read(1)[0]) << (22 - f_pos)
                    f_pos += 8

                    if f_buf >> 29:
                        if (f_buf >> 28) == 0b11:
                            match_size = 1
                            delta = 2
                        else:
                            proposed_code = f_buf >> 26
                            delta = 4
                            if proposed_code == 0b1000:
                                match_size = 2
                            elif proposed_code == 0b1011:
                                match_size = 3
                            else:
                                if (f_buf >> 25) == 0b10100:
                                    match_size = 4
                                    delta = 5
                                else:
                                    proposed_code = f_buf >> 24
                                    delta = 6
                                    if proposed_code == 0b100100:
                                        match_size = 5
                                    elif proposed_code == 0b100110:
                                        match_size = 6
                                    elif proposed_code == 0b101011:
                                        match_size = 7
                                    elif proposed_code == 0b100111:
                                        match_size = 13
                                    else:
                                        proposed_code = f_buf >> 23
                                        delta = 7
                                        if proposed_code == 0b1001011:
                                            match_size = 8
                                        elif proposed_code == 0b1010100:
                                            match_size = 9
                                        else:
                                            proposed_code = f_buf >> 22
                                            delta = 8
                                            if proposed_code == 0b10010100:
                                                match_size = 10
                                            elif proposed_code == 0b10010101:
                                                match_size = 11
                                            elif proposed_code == 0b10101010:
                                                match_size = 12
                                            elif proposed_code == 0b10101011:
                                                f_pos = f_buf = 0  # FLUSH
                                                continue
                                            else:
                                                break  # no valid code detected
                    else:  # 0b0
                        match_size = 0
                        delta = 1

                    f_buf = (f_buf << delta) & full_mask
                    f_pos -= delta

                    match_size += min_pattern_size
                    while f_pos < w_bits:
                        f_buf |= int(f.read(1)[0]) << (22 - f_pos)
                        f_pos += 8

                    index = f_buf >> (30 - w_bits)
                    f_buf = (f_buf << w_bits) & full_mask
                    f_pos -= w_bits

                    # Copy bytes from window to output or overflow buffer.
                    is_overlap = (w_pos - index) & w_mask < match_size
                    copy_right_to_left = is_overlap and w_pos > index
                    for i in range(match_size):
                        j = match_size - i - 1 if copy_right_to_left else i
                        out_index = out_pos + j
                        if out_index < out_capacity:
                            out_buf[out_index] = w_buf[index + j]
                        else:
                            overflow_index = (overflow_pos + overflow_size) % max_pattern_size
                            overflow_buf[overflow_index] = w_buf[index + j]
                            overflow_size += 1

                        w_buf[(w_pos + j) & w_mask] = w_buf[index + j]

                out_pos += match_size
                w_pos = (w_pos + match_size) & w_mask
        except IndexError:
            pass

        if out_pos > out_capacity:
            out_pos = out_capacity

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
