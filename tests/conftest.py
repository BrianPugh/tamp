def pytest_addoption(parser):
    parser.addoption(
        "--dataset",
        action="store_true",
        default=False,
        help="Run dataset regression tests (requires LFS files).",
    )
