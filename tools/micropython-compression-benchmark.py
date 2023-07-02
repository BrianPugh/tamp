from pathlib import Path

import tamp

chunk_size = 1 << 20

compressor = tamp.Compressor("build/enwik8.tamp")

with open("build/enwik8", "rb") as f:
    i = 0
    while True:
        i += 1
        print(f"Chunk {i}")
        chunk = f.read(chunk_size)
        if not chunk:
            break
        compressor.write(chunk)
    compressor.flush(write_token=False)
print("Complete!")
