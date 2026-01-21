"""Goal to increase hufman decoding speed via lookup table.

1. Look at first bit, if it's 0, then stop decoding and return 0. Maybe do the samething for 2nd bit.
2. Else, read the next 7 bits, and use the value to index into a table.
3. Use the upper 4 bits to express the number of bits decoded.
4. Use the lower 4 bits to express the decoded value.
"""

_FLUSH = 15

table_bits = 7

huffman_lookup = {
    "1": 1,
    "000": 2,
    "011": 3,
    "0100": 4,
    "00100": 5,
    "00110": 6,
    "01011": 7,
    "001011": 8,
    "010100": 9,
    "0010100": 10,
    "0010101": 11,
    "0101010": 12,
    "00111": 13,
    "0101011": _FLUSH,
}

table_size = 1 << table_bits

UNPOPULATED = object()
c_table = [UNPOPULATED] * table_size
py_table = [UNPOPULATED] * table_size

for k, v in huffman_lookup.items():
    n_bits = len(k)
    n_pad = table_bits - n_bits
    for j in range(1 << n_pad):
        index_bin_str = k
        if n_pad:
            index_bin_str += format(j, f"0{n_pad}b")
        index = int(index_bin_str, 2)
        c_table[index] = v | (n_bits << 4)
        py_table[index] = v | ((n_bits + 1) << 4)

print(f"const uint8_t HUFFMAN_TABLE[{table_size}] = {{{str(c_table)[1:-1]}}};")
print(f"_HUFFMAN_TABLE = {bytes(py_table)}")
