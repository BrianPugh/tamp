import hashlib
import unittest
from pathlib import Path

import pytest

from tamp._c_compressor import compress as c_compress
from tamp._c_decompressor import decompress as c_decompress

PROJECT_DIR = Path(__file__).parent.parent

# Regression fixtures for the compressed output of known corpora.
#
# Rather than committing the (large) ground-truth ``.tamp`` binaries via git LFS,
# we store only the SHA256 of the compressed output here. Each test compresses
# the source corpus with the current C implementation and asserts the compressed
# bytes hash to the recorded value (format-stability regression), then decompresses
# and asserts a byte-exact round trip (correctness).
#
# The source corpora are fetched with ``make download-enwik8 download-silesia``;
# the uf2 fixture lives at ``datasets/RPI_PICO-20250415-v1.25.0.uf2``. Tests for a
# corpus that isn't present locally are skipped (use ``--no-dataset`` to skip all).
#
# To regenerate the ``.tamp`` ground-truth binaries (e.g. for cross-checking against
# another implementation), see ``make v1-compressed-datasets`` /
# ``make extended-compressed-datasets``; the SHA256s below identify the exact files
# those targets must produce.
#
# Compression settings are the library defaults (window=10, literal=8, no lazy
# matching); only the ``extended`` flag varies between the two formats.

# (source_rel_path, v1_compressed_sha256, extended_compressed_sha256)
DATASETS = [
    (
        "datasets/enwik8",
        "02e05af059a0040d641988075cf1dfc479a084f9a34b5c8a348354211c5fa038",
        "d9d804c91b4dc5e81856db074760037040421cfa84e1ea211e16dde8c295ce6d",
    ),
    (
        "datasets/RPI_PICO-20250415-v1.25.0.uf2",
        "b14029d9171b6fdff493b03db6634cf20d654674fbd68c0ea6e1b321af1a4aad",
        "94be865699e667ceffb7bae10bc5c040fe6cdc22b9e02e61bd88363d767ec171",
    ),
    (
        "datasets/silesia/dickens",
        "caacd14fc2a6bbf5b8ee13b49f94d1ddab2b8eba8e6379685ee67f6b309f33fd",
        "01482dc4fa9c7c7674eca711a378d8d1ac7c59615b326a8d9a06f09bc9f33c30",
    ),
    (
        "datasets/silesia/mozilla",
        "5a4d69d28071e4327917be293ff4c9cd9c1b362a48d8f15d5693b786622e7343",
        "53497c04175742c323b7e086a4f0ce44533c8f238ef85562591c79a587a81679",
    ),
    (
        "datasets/silesia/mr",
        "7862014d14955b765959ddc875f00f36401b49654e75a564f9313b9ac04c35dd",
        "94f27dfffe842fff3669e80eb5a3c769c33cd591f08001ae5e2eb95ff152a1fc",
    ),
    (
        "datasets/silesia/nci",
        "cb20a8e688046e36d123d2528c16c12dfa75e6cb1ae6e7bbf7d634f80ea7ae07",
        "0acbb7c787947df92ab4a44d4259f52d327d98dd2ec5860a998c51ed32409753",
    ),
    (
        "datasets/silesia/ooffice",
        "8d7c35aae8fc664d89350fbf6162db34151f7ef85175c555d4c366f23ecd2b80",
        "ae3bcd7bbff8f223a2c4ca937a5e2eafd793c8abda48d203ed110e98f2896526",
    ),
    (
        "datasets/silesia/osdb",
        "2e28f5f7cb1173a288cdaadfedb3a554ed50af3bb297fe029964876d7345d69b",
        "1eb802368db32844da9c195095cb6727f0c39f719325ac23c446cee2de1d557d",
    ),
    (
        "datasets/silesia/reymont",
        "f81ae5f8ae000a3ba898efea3c4b303628fe2d3d432f8913f07d5feac9274e3f",
        "4024c6820a3af628dd2059c9261b2713c9a8f48b4a26e57c45a9776fec9b8068",
    ),
    (
        "datasets/silesia/samba",
        "41a7d7d82211282c56c538e78376e8454867eb83cd0b7c1722135b9590d92306",
        "f13c7e856a8e0366b7c2fa58de56bde427bf1840335e596a70db46a6aff4feec",
    ),
    (
        "datasets/silesia/sao",
        "3b041d04441550f118ae2e0a179e32a43c8cfe364daa9c5233a316932bb35a62",
        "8c05ac1c7d78b04874f07e10265cd254ecf9d6dcf1a3f0d1ea695815509ff0b1",
    ),
    (
        "datasets/silesia/webster",
        "dec3393e12db417223069da7b77cd458f08f288ee5bf456ea48656a9024674e8",
        "91d3c9ddb94e370d4f9cb518e846fda8a197bd9ef4ea4fcb91f84688af49316a",
    ),
    (
        "datasets/silesia/x-ray",
        "933bbf2441f5938a80a6b4817160c86e77f47c48ab84612adceb45d728efdfd5",
        "4ba0c1fb79addae24888c12a466e84b73c32ca608836c458487226d224a63fc3",
    ),
    (
        "datasets/silesia/xml",
        "1340ee234214f06e36b43bd3089d56b29ae9fa31ef945c75d9b23ab67cb89bc6",
        "11a9d7e077f31a83b06cbcfb95c6ef2d74355e6f6af9decba3c2e3529e1e903f",
    ),
]


def _check(rel_path, expected_compressed_sha256, *, extended):
    source = PROJECT_DIR / rel_path
    if not source.is_file():
        raise unittest.SkipTest(f"source corpus not found: {rel_path} (run `make download-enwik8 download-silesia`)")

    data = source.read_bytes()
    compressed = c_compress(data, extended=extended)

    actual = hashlib.sha256(compressed).hexdigest()
    assert actual == expected_compressed_sha256, f"compressed SHA256 mismatch for {rel_path} (extended={extended})"

    # Round-trip: the compressed stream must decompress back to the original bytes.
    assert c_decompress(compressed) == data, f"round-trip mismatch for {rel_path} (extended={extended})"


class TestV1Compression(unittest.TestCase):
    @pytest.mark.dataset
    def test_v1_compress(self):
        for rel_path, v1_sha256, _ in DATASETS:
            with self.subTest(dataset=rel_path):
                _check(rel_path, v1_sha256, extended=False)


class TestExtendedCompression(unittest.TestCase):
    @pytest.mark.dataset
    def test_extended_compress(self):
        for rel_path, _, extended_sha256 in DATASETS:
            with self.subTest(dataset=rel_path):
                _check(rel_path, extended_sha256, extended=True)


if __name__ == "__main__":
    unittest.main()
