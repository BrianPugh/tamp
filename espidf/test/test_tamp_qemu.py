"""
QEMU tests for Tamp ESP32 implementation using pytest-embedded.

This is the standard Espressif approach for automated QEMU testing.
"""

import pytest
from pytest_embedded import Dut


@pytest.mark.esp32
@pytest.mark.esp32s3
@pytest.mark.qemu
@pytest.mark.generic
def test_tamp_qemu(dut: Dut) -> None:
    """Test Tamp compression/decompression in QEMU."""
    # Wait for test suite banner
    dut.expect("TAMP ESP32 QEMU Test Suite", timeout=10)

    # Check for test completion
    dut.expect("ALL TESTS PASSED", timeout=30)
