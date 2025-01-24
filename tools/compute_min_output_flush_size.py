"""Computes what the minimum output buffer size should be to handle a worse-case tamp_compressor_flush."""
from tamp import compute_min_pattern_size


def bits2bytes(val):
    if val % 8 == 0:
        return val // 8
    return int((val // 8) + 1)


for literal in [5, 6, 7, 8]:
    for window in range(8, 16):
        is_literal_bit = 1
        smallest_huffman = 1
        biggest_huffman = 8
        input_buffer_size = 16
        flush_code = 9  # contains an implicit is_literal_bit
        min_pattern_size = compute_min_pattern_size(window, literal)

        # The biggest amount of bits that could potentially be residing in the internal output bit buffer.
        biggest_residual_output = 7 + is_literal_bit + biggest_huffman + window

        # The largest output is achieved if all bytes in the input buffer are interpreted as literals and cannot be compressed.
        without_write = biggest_residual_output + input_buffer_size * (is_literal_bit + literal)

        # attempting to use a flush token will either add 0 bits if perfectly byte-aligned, or 9 bits if not.
        # Aside from literals, the worst compression ratio is achieved at min_pattern_size.
        # if without_write is perfectly byte-aligned, we have to check what would happen with a
        # single min_pattern_size compression, which would then potentially result in a flush token.
        if without_write % 8 == 0:
            with_write = (
                biggest_residual_output
                + (input_buffer_size - min_pattern_size) * (is_literal_bit + literal)
                + (is_literal_bit + smallest_huffman + window)
                + flush_code
            )
            with_write = max(with_write, without_write)
        else:
            with_write = without_write + flush_code

        without_write_bytes = bits2bytes(without_write)
        with_write_bytes = bits2bytes(with_write)

        print(f"{literal=} {window=} {without_write_bytes=} {with_write_bytes=}")
    print()
