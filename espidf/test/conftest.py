"""
pytest configuration for Tamp ESP32 QEMU tests.
"""

import os

import pytest


@pytest.fixture(scope="module")
def app_path(request):
    """Path to the ESP-IDF test application."""
    # Return the current directory - this is where CMakeLists.txt and the app are
    return os.path.dirname(request.module.__file__)
