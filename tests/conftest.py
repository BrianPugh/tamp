import pytest


def pytest_addoption(parser):
    parser.addoption(
        "--no-dataset",
        action="store_true",
        default=False,
        help="Skip dataset regression tests (which require LFS files).",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "dataset: mark test as requiring dataset files (use --no-dataset to skip)")


def pytest_collection_modifyitems(config, items):
    if not config.getoption("--no-dataset"):
        # --no-dataset not given: run dataset tests
        return
    skip_dataset = pytest.mark.skip(reason="--no-dataset option used")
    for item in items:
        if "dataset" in item.keywords:
            item.add_marker(skip_dataset)
