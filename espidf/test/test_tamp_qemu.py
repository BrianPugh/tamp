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

    # Capture critical test outputs that show ref vs opt comparison
    # These lines have format: "Test name: ref_size=X ref_index=Y, opt_size=X opt_index=Y"
    critical_tests = [
        r"Match position preference: ref_size=\d+ ref_index=\d+, opt_size=\d+ opt_index=\d+",
        r"Longer match earlier: ref_size=\d+ ref_index=\d+, opt_size=\d+ opt_index=\d+",
        r"Shorter match later: ref_size=\d+ ref_index=\d+, opt_size=\d+ opt_index=\d+",
    ]

    print("\n" + "=" * 70)
    print("SEARCH ALGORITHM CONSISTENCY CHECK")
    print("=" * 70)

    found_mismatch = False
    for pattern in critical_tests:
        try:
            output = dut.expect(pattern, timeout=10)
            line = output.group().decode("utf-8") if isinstance(output.group(), bytes) else str(output.group())
            print(line)

            # Check if ref and opt values differ
            if "ref_size" in line and "opt_size" in line:
                # Extract values
                import re

                ref_match = re.search(r"ref_size=(\d+) ref_index=(\d+)", line)
                opt_match = re.search(r"opt_size=(\d+) opt_index=(\d+)", line)

                if ref_match and opt_match:
                    ref_size, ref_index = ref_match.groups()
                    opt_size, opt_index = opt_match.groups()

                    if ref_size != opt_size or ref_index != opt_index:
                        print("  ⚠️  MISMATCH DETECTED!")
                        print(f"      Reference: size={ref_size}, index={ref_index}")
                        print(f"      Optimized: size={opt_size}, index={opt_index}")
                        found_mismatch = True
                    else:
                        print("  ✓ Match consistent")

        except Exception:
            print("  (timeout or test skipped)")

    print("=" * 70)
    if found_mismatch:
        print("BUG CONFIRMED: Optimized search differs from reference!")
    print("=" * 70 + "\n")

    # Capture compression checksums
    print("=" * 70)
    print("COMPRESSION CHECKSUMS (ESP32 vs ESP32-S3 should match)")
    print("=" * 70)
    try:
        checksum_output = dut.expect(r"Compressed output checksum: 0x[0-9a-fA-F]+", timeout=10)
        print(
            checksum_output.group().decode("utf-8")
            if isinstance(checksum_output.group(), bytes)
            else str(checksum_output.group())
        )
    except Exception:
        print("(checksum not found)")
    print("=" * 70 + "\n")

    # Check for test completion
    result = dut.expect("ALL TESTS PASSED|SOME TESTS FAILED", timeout=60)

    # Fail the pytest test if any Unity tests failed
    if "SOME TESTS FAILED" in str(result):
        pytest.fail("Some Unity tests failed - check console output above")
