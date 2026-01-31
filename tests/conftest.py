import pytest


def pytest_addoption(parser):
    parser.addoption(
        "--dataset",
        action="store_true",
        default=False,
        help="Run dataset regression tests (requires LFS files).",
    )


def pytest_configure(config):
    config.addinivalue_line("markers", "dataset: mark test as requiring dataset files (use --dataset to run)")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--dataset"):
        # --dataset given in cli: do not skip dataset tests
        return
    skip_dataset = pytest.mark.skip(reason="need --dataset option to run")
    for item in items:
        if "dataset" in item.keywords:
            item.add_marker(skip_dataset)
