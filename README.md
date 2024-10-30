# Tamp for ESP32

This is a modification of the Tamp compression library created by <span
class="title-ref">Brian Pugh \<https://github.com/BrianPugh\></span>,
aggressively speed-optimized for use as a C/C++ library on ESP32 MCUs.

The C implementation of the original Tamp library is made into a
component for use in ESP-IDF builds and speed-optimized for 32-bit
architectures like the ESP32s', including faster pattern-match search
functions.

While the original Tamp C code is compatible with most (/all?)
architectures, this optimized variant leverages the ESP32s' ability to
do unaligned 16- and 32-bit memory accesses. It should be able to run on
architectures other than ESP32s (e.g. other RISC-V CPUs), but it will
*not* work on chips which don't support unaligned memory accesses (e.g.
the Raspberry Pi Pico RP2040).

The performance optimizations work out to about 2.5-4x faster
compression on ESP32s (both Xtensa and RISC-V, i.e. ESP32, ESP32-C2,
ESP32-S3, &c.). When built for an **ESP32-S3**, the MCU's SIMD
instructions ("PIE") are used which increases compression speed/
throughput by a factor of 8-20x.

When building the C library as a component in an ESP-IDF build, you can
choose to build the original or the ESP32-optimized variant in
menuconfig.

# Benchmarks

Tamp can deliver decent compression with minimal memory and CPU
requirements.

## Compression ratio

Input: 5614 bytes of text

Compressed output in bytes vs. window size ("window bits (bytes)"), i.e.
RAM requirement

|             | Uncompressed | 8 (256b) | 9 (512b) | 10 (1kb) | 11 (2kb) | 12 (4kb) |
|-------------|--------------|----------|----------|----------|----------|----------|
| Bytes       | 5614         | 3195     | 2800     | 2649     | 2581     | 2571     |
| %           | 100,0 %      | 56,9 %   | 49,9 %   | 47,2 %   | 46,0 %   | 45,8 %   |
| Compression | 1,0 x        | 1,8 x    | 2,0 x    | 2,1 x    | 2,2 x    | 2,2 x    |

Input: Espressif logo in 24-bit BMP format, 25862 bytes

Compressed output in bytes vs. window size ("window bits (bytes)")

|             | Uncompressed | 8 (256b) | 9 (512b) | 10 (1kb) | 11 (2kb) | 12 (4kb) | 13 (8kb) |
|-------------|--------------|----------|----------|----------|----------|----------|----------|
| Bytes       | 25862        | 7720     | 7772     | 6538     | 6517     | 6601     | 6590     |
| %           | 100,0 %      | 29,9 %   | 30,1 %   | 25,3 %   | 25,2 %   | 25,5 %   | 25,5 %   |
| Compression | 1,0 x        | 3,4 x    | 3,3 x    | 4,0 x    | 4,0 x    | 3,9 x    | 3,9 x    |

As to the overall compression efficiency, 7zip in "Ultra" mode
compresses the same file down to 3022 bytes (11,7%), which should be a
rough approximation of the entropy of that file, i.e. the theoretical
limit of what is possible.

## Speed

Performance on an **ESP32-S3** (Xtensa) @ **240 MHz**:

Input: Espressif logo in 24-bit BMP format, 25862 bytes

**Original C** version:

|            | 8          | 9          | 10        | 11        | 12        | 13        |
|------------|------------|------------|-----------|-----------|-----------|-----------|
| CPU time   | 103,5 ms   | 182,1 ms   | 252,7 ms  | 420,7 ms  | 782,1 ms  | 1419,1 ms |
| Throughput | 244,0 kb/s | 138,7 kb/s | 99,9 kb/s | 60,0 kb/s | 32,3 kb/s | 17,8 kb/s |

**Optimized C/C++** variant:

|             | 8          | 9          | 10         | 11         | 12         | 13        |
|-------------|------------|------------|------------|------------|------------|-----------|
| CPU time    | 35,4 ms    | 56,6 ms    | 73,3 ms    | 122,3 ms   | 221,9 ms   | 395,9 ms  |
| Throughput  | 713,1 kb/s | 446,6 kb/s | 344,6 kb/s | 206,6 kb/s | 113,8 kb/s | 63,8 kb/s |
| Speed (rel) | 2,9 x      | 3,2 x      | 3,4 x      | 3,4 x      | 3,5 x      | 3,6 x     |

**ESP32-S3 SIMD/PIE** variant:

|             | 8           | 9           | 10          | 11          | 12         | 13         |
|-------------|-------------|-------------|-------------|-------------|------------|------------|
| CPU time    | 13,0 ms     | 15,4 ms     | 16,9 ms     | 23,6 ms     | 35,9 ms    | 59,1 ms    |
| Throughput  | 1948,2 kb/s | 1636,2 kb/s | 1493,0 kb/s | 1070,4 kb/s | 703,9 kb/s | 427,1 kb/s |
| Speed (rel) | 8,0 x       | 11,8 x      | 14,9 x      | 17,8 x      | 21,8 x     | 24,0 x     |

An **ESP32-C3** (RISC-V, max. 160 MHz) consistently runs the original
variant about 5% faster and the optimized C/C++ variant about 10% faster
*per CPU clock cycle* than the ESP32-S3; so the C3 is, for this
workload, a little more efficient than the S3.
