"""Builds the Cython extensions; all other packaging configuration is in pyproject.toml."""

import os
import platform
import sys

from setuptools import setup

# Don't allow failure if cibuildwheel is running.
allowed_to_fail = os.environ.get("CIBUILDWHEEL", "0") != "1"

profile = os.environ.get("TAMP_PROFILE", "0") == "1"

# Enable sanitizers only when explicitly requested; disable for CI/production.
in_ci = os.environ.get("CI", "false").lower() == "true"
building_wheel = os.environ.get("CIBUILDWHEEL", "0") == "1"
is_production_build = in_ci or building_wheel

sanitize = os.environ.get("TAMP_SANITIZE", "0") == "1"

# Ensure profile and sanitize are mutually exclusive
if profile and sanitize:
    raise ValueError(
        "Cannot enable both profiling and sanitizers at the same time. "
        "Please choose either TAMP_PROFILE=1 or TAMP_SANITIZE=1, not both."
    )

# Sanitizer flags below are UNIX-only.
if sanitize and os.name == "nt":
    raise ValueError("TAMP_SANITIZE=1 is not supported on Windows.")


def build_extensions():
    import Cython.Compiler.Options
    from Cython.Build import cythonize
    from Cython.Compiler.Options import get_directive_defaults
    from setuptools import Extension

    Cython.Compiler.Options.annotate = True

    if sanitize:
        print("Building with sanitizers enabled (UBSan + ASan)")
    elif is_production_build:
        print("Building for production (sanitizers disabled)")
    else:
        print("Building for development")

    define_macros = []

    define_macros.append(("TAMP_LAZY_MATCHING", "1"))

    # Force embedded find_best_match implementation on desktop (for testing)
    if os.environ.get("TAMP_USE_EMBEDDED_MATCH", "0") == "1":
        print("Using embedded find_best_match implementation")
        define_macros.append(("TAMP_USE_EMBEDDED_MATCH", "1"))
    elif sys.maxsize > 2**32 and platform.machine().lower() in ("x86_64", "amd64", "arm64", "aarch64"):
        # The core C sources default to portable code everywhere; each build
        # system opts into its platform's measured configuration (see the
        # platform tuning section in tamp/_c_src/tamp/common.h).
        # platform.machine() reports the OS architecture, so the sys.maxsize
        # check is what excludes 32-bit interpreters on 64-bit hosts (win32
        # cibuildwheel legs, linux32-personality containers) - the desktop
        # matcher needs a 64-bit target (e.g. MSVC's _BitScanForward64 does
        # not exist on 32-bit targets).
        define_macros.append(("TAMP_USE_DESKTOP_MATCH", "1"))

    if profile:
        print("Setting profiling configuration.")
        directive_defaults = get_directive_defaults()
        directive_defaults["linetrace"] = True
        directive_defaults["binding"] = True

        define_macros.append(("CYTHON_TRACE", "1"))

    extra_link_args = []
    if os.name == "nt":  # Windows
        if profile:  # noqa: SIM108
            extra_compile_args = [
                "/O2",
                "/Z7",  # Debug info for profiling
            ]
        else:
            extra_compile_args = [
                "/O2",
            ]
    else:  # UNIX-based systems
        if sanitize:
            extra_compile_args = [
                "-O0",  # No optimization for better sanitizer output
                "-g",  # Include debug symbols
                "-fsanitize=undefined",
                "-fsanitize=address",
                "-fno-omit-frame-pointer",
                "-Wno-unreachable-code-fallthrough",
                "-Wno-deprecated-declarations",
                "-Wno-parentheses-equality",
                "-Wno-unreachable-code",
            ]
            extra_link_args = ["-fsanitize=undefined", "-fsanitize=address"]
        elif profile:
            extra_compile_args = [
                "-O2",  # Use O2 instead of O3 for better debug info
                "-g",  # Include debug symbols
                "-fno-omit-frame-pointer",
                "-Wno-unreachable-code-fallthrough",
                "-Wno-deprecated-declarations",
                "-Wno-parentheses-equality",
                "-Wno-unreachable-code",
            ]
        else:
            extra_compile_args = [
                "-O3",
                "-Werror",
                "-fno-omit-frame-pointer",  # counterintuitively, makes things faster?
                "-Wno-unreachable-code-fallthrough",
                "-Wno-deprecated-declarations",
                "-Wno-parentheses-equality",
                "-Wno-unreachable-code",  # TODO: This should no longer be necessary with Cython>=3.0.3
                # https://github.com/cython/cython/issues/5681
            ]
            # GCC leaves the symbol table + debug info in the .so, bloating the
            # Linux wheel ~7x (2.0MB vs 0.27MB). macOS keeps debug info in a
            # separate .dSYM (and its ld rejects -s); Windows uses a .pdb. So
            # strip only on Linux. The sanitize/profile branches keep -g.
            if sys.platform.startswith("linux"):
                extra_link_args = ["-Wl,--strip-all"]
    include_dirs = ["tamp/_c_src/", "tamp/"]

    extension_kwargs = {
        "include_dirs": include_dirs,
        "extra_compile_args": extra_compile_args,
        "extra_link_args": extra_link_args,
        "language": "c",
        "define_macros": define_macros,
        "optional": allowed_to_fail,
    }

    extensions = [
        Extension(
            "tamp._c_compressor",
            [
                "tamp/_c_src/tamp/common.c",
                "tamp/_c_src/tamp/compressor.c",
                "tamp/_c_compressor.pyx",
            ],
            **extension_kwargs,
        ),
        Extension(
            "tamp._c_decompressor",
            [
                "tamp/_c_src/tamp/common.c",
                "tamp/_c_src/tamp/decompressor.c",
                "tamp/_c_decompressor.pyx",
            ],
            **extension_kwargs,
        ),
        Extension(
            "tamp._c_build_dictionary",
            [
                "tamp/_c_build_dictionary.pyx",
            ],
            **extension_kwargs,
        ),
        Extension(
            "tamp._c_common",
            [
                "tamp/_c_common.pyx",
            ],
            **extension_kwargs,
        ),
    ]

    # Configure cythonize options for profiling
    cythonize_kwargs = {
        "include_path": include_dirs,
        "language_level": 3,
        "annotate": True,
    }

    if profile:
        # Enable additional debugging for profiling
        cythonize_kwargs["gdb_debug"] = True
        cythonize_kwargs["emit_linenums"] = True

    return cythonize(extensions, **cythonize_kwargs)


if os.environ.get("TAMP_BUILD_C_EXTENSIONS", "1") == "1":
    setup(ext_modules=build_extensions())
else:
    setup()
