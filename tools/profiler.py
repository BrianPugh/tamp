from io import BytesIO

import line_profiler

import tamp
import tamp._c

profile = line_profiler.LineProfiler(tamp.Compressor.write)

with open("build/enwik8", "rb") as f, BytesIO() as f_compressed:
    data = f.read()
    compressor = tamp._c.Compressor(f_compressed)
    profile.runcall(compressor.write, data)

profile.print_stats()
