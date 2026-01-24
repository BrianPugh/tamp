import argparse
import collections
import contextlib
import multiprocessing
import random
from io import BytesIO
from pathlib import Path

from tqdm import tqdm

from tamp import initialize_dictionary
from tamp.compressor import Compressor


def random_slices(data, num_slices, slice_size):
    slices = []
    for _ in range(num_slices):
        start_index = random.randint(0, len(data) - slice_size)  # noqa: S311
        end_index = start_index + slice_size
        slices.append(data[start_index:end_index])

    return slices


def compute_size(seed, data):
    size = 0
    for window_bits, chunks in data.items():
        for chunk in chunks:
            with BytesIO() as f:
                dictionary = initialize_dictionary(1 << window_bits, seed=seed)
                compressor = Compressor(f, window=window_bits, dictionary=dictionary)
                compressor.write(chunk)
                compressor.flush()
                size += f.tell()
    return size


def find_seed(best_seed, best_size, lock, data, start_index, end_index):
    random.seed()
    with contextlib.suppress(KeyboardInterrupt):
        generator = range(start_index, end_index)
        if start_index == 0:
            generator = tqdm(generator)
        for seed in generator:
            size = compute_size(seed, data)
            with lock:
                if size < best_size.value:
                    best_seed.value = seed
                    best_size.value = size
                    print(f"{seed=} {size=}")


def read_data():
    input_folder = Path("datasets/silesia")
    files = list(input_folder.glob("*"))
    data_list = [x.read_bytes() for x in files]
    return data_list


def generate_data(data_list, chunks_per_source):
    sliced_data = {
        8: [],
        9: [],
        10: [],
    }
    for data in data_list:
        for k in sliced_data:
            sliced_data[k].extend(random_slices(data, chunks_per_source, 1 << k))
    return sliced_data


def character_finder(data_list, n):
    counter = collections.Counter(b"".join(data_list))
    most_common = counter.most_common(n)
    common_bytes = bytes(x[0] for x in most_common)
    print(f"{common_bytes=}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=-1, help="Confirm seed performance.")
    parser.add_argument("--character-finder", type=int, default=-1)
    parser.add_argument("--processes", type=int, default=8)
    args = parser.parse_args()

    chunks_per_source = 200

    random.seed(100)

    data_list = read_data()
    sliced_data = generate_data(data_list, chunks_per_source)

    uncompressed_size = 0
    total_chunks = 0
    for v in sliced_data.values():
        uncompressed_size += len(b"".join(v))
        total_chunks += len(v)
    print(f"{uncompressed_size=} {total_chunks=}")

    if args.seed >= 0:
        seed = args.seed
        size = compute_size(args.seed, sliced_data)
        print(f"{seed=} {size=}")
        return

    if args.character_finder >= 0:
        character_finder(data_list, args.character_finder)
        return

    shared_best_seed = multiprocessing.Value("i", 0)
    shared_best_size = multiprocessing.Value("i", 10000000000000)
    lock = multiprocessing.Lock()

    intervals = list(range(0, 0xFFFFFFFF, 0xFFFFFFFF // args.processes))
    processes = []
    for start_index, end_index in zip(intervals[:-1], intervals[1:]):
        processes.append(
            multiprocessing.Process(
                target=find_seed,
                args=(
                    shared_best_seed,
                    shared_best_size,
                    lock,
                    sliced_data,
                    start_index,
                    end_index,
                ),
            )
        )

    with contextlib.suppress(KeyboardInterrupt):
        for process in processes:
            process.start()

        for process in processes:
            process.join()


if __name__ == "__main__":
    main()
