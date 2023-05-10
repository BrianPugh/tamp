from io import BytesIO

import line_profiler

import tamp
import tamp._c

with open("build/enwik8", "rb") as f, BytesIO() as f_compressed:
    data = f.read()

    compressor = tamp._c.Compressor(f_compressed)
    profile = line_profiler.LineProfiler(compressor.write)
    profile.runcall(compressor.write, data)

profile.print_stats()
