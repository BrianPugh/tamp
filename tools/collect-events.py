from io import BytesIO
from pathlib import Path
from time import monotonic

from cyclopts import App

from tamp.compressor import Compressor  # Explicitly the python implementation

app = App()


@app.default
def main(
    src: Path,
    dst: Path = Path("events"),
    window: int = 10,
):
    plaintext = src.read_bytes()

    def token_cb(offset, match_size, pattern):
        pass

    def literal_cb(char):
        pass

    def flush_cb():
        pass

    with BytesIO() as compressed_out:
        compressor = Compressor(compressed_out, window=window)

        compressor.token_cb = token_cb
        compressor.literal_cb = literal_cb
        compressor.flush_cb = flush_cb

        t_start = monotonic()
        compressor.write(plaintext)
        compressor.flush(write_token=False)
        t_elapsed = monotonic() - t_start

        compressed_size = compressed_out.tell()

    print(f"{compressed_size=}")
    print(f"{t_elapsed=}")


if __name__ == "__main__":
    app()
