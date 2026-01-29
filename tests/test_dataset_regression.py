import hashlib
import os
import unittest

import tamp

PROJECT_DIR = os.path.dirname(os.path.dirname(__file__))

# Test both C and Python implementations
from tamp._c_decompressor import decompress as c_decompress
from tamp.decompressor import decompress as py_decompress

DECOMPRESSOR_IMPLEMENTATIONS = [
    ("C (Cython)", c_decompress),
    ("Python", py_decompress),
]

# Expected SHA256 hashes of the *decompressed* content.
V1_DATASETS = [
    (
        "datasets/v1-compressed/enwik8.tamp",
        "2b49720ec4d78c3c9fabaee6e4179a5e997302b3a70029f30f2d582218c024a8",
    ),
    (
        "datasets/v1-compressed/RPI_PICO-20250415-v1.25.0.uf2.tamp",
        "e0c40eacf1afc550a6add74888c48bb981b28788a6d75a62a0e2444e997b9864",
    ),
    (
        "datasets/v1-compressed/silesia/dickens.tamp",
        "b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a",
    ),
    (
        "datasets/v1-compressed/silesia/mozilla.tamp",
        "657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b",
    ),
    (
        "datasets/v1-compressed/silesia/mr.tamp",
        "68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6",
    ),
    (
        "datasets/v1-compressed/silesia/nci.tamp",
        "fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635",
    ),
    (
        "datasets/v1-compressed/silesia/ooffice.tamp",
        "e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb",
    ),
    (
        "datasets/v1-compressed/silesia/osdb.tamp",
        "60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3",
    ),
    (
        "datasets/v1-compressed/silesia/reymont.tamp",
        "0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578",
    ),
    (
        "datasets/v1-compressed/silesia/samba.tamp",
        "93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2",
    ),
    (
        "datasets/v1-compressed/silesia/sao.tamp",
        "c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc",
    ),
    (
        "datasets/v1-compressed/silesia/webster.tamp",
        "6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383",
    ),
    (
        "datasets/v1-compressed/silesia/x-ray.tamp",
        "7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad",
    ),
    (
        "datasets/v1-compressed/silesia/xml.tamp",
        "0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c",
    ),
]


class TestV1Decompression(unittest.TestCase):
    def test_v1_decompress(self):
        for impl_name, decompress_func in DECOMPRESSOR_IMPLEMENTATIONS:
            for rel_path, expected_sha256 in V1_DATASETS:
                with self.subTest(implementation=impl_name, dataset=rel_path):
                    path = os.path.join(PROJECT_DIR, rel_path)

                    with open(path, "rb") as f:
                        data = f.read()

                    decompressed = decompress_func(data)
                    actual = hashlib.sha256(decompressed).hexdigest()
                    self.assertEqual(actual, expected_sha256, f"SHA256 mismatch for {rel_path} using {impl_name}")


if __name__ == "__main__":
    unittest.main()
