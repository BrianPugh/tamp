import os
import shutil
from pathlib import Path

allowed_to_fail = os.environ.get("CIBUILDWHEEL", "0") != "1"

profile = os.environ.get("TAMP_PROFILE", "0") == "1"


def build_cython_extensions():
    import Cython.Compiler.Options
    from Cython.Build import build_ext, cythonize
    from Cython.Compiler.Options import get_directive_defaults
    from setuptools import Extension
    from setuptools.dist import Distribution

    Cython.Compiler.Options.annotate = True

    define_macros = []

    define_macros.append(("TAMP_LAZY_MATCHING", "1"))

    if profile:
        print("Setting profiling configuration.")
        directive_defaults = get_directive_defaults()
        directive_defaults["linetrace"] = True
        directive_defaults["binding"] = True

        define_macros.append(("CYTHON_TRACE", "1"))

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
        if profile:
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
    include_dirs = ["tamp/_c_src/", "tamp/"]

    extensions = [
        Extension(
            "tamp._c_compressor",
            [
                "tamp/_c_src/tamp/common.c",
                "tamp/_c_src/tamp/compressor.c",
                "tamp/_c_compressor.pyx",
            ],
            include_dirs=include_dirs,
            extra_compile_args=extra_compile_args,
            language="c",
            define_macros=define_macros,
        ),
        Extension(
            "tamp._c_decompressor",
            [
                "tamp/_c_src/tamp/common.c",
                "tamp/_c_src/tamp/decompressor.c",
                "tamp/_c_decompressor.pyx",
            ],
            include_dirs=include_dirs,
            extra_compile_args=extra_compile_args,
            language="c",
            define_macros=define_macros,
        ),
        Extension(
            "tamp._c_common",
            [
                "tamp/_c_common.pyx",
            ],
            include_dirs=include_dirs,
            extra_compile_args=extra_compile_args,
            language="c",
            define_macros=define_macros,
        ),
    ]

    include_dirs = set()
    for extension in extensions:
        include_dirs.update(extension.include_dirs)
    include_dirs = list(include_dirs)

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

    ext_modules = cythonize(extensions, **cythonize_kwargs)
    dist = Distribution({"ext_modules": ext_modules})
    cmd = build_ext(dist)
    cmd.ensure_finalized()
    cmd.run()

    for output in cmd.get_outputs():
        output = Path(output)
        relative_extension = output.relative_to(cmd.build_lib)
        shutil.copyfile(output, relative_extension)


if os.environ.get("TAMP_BUILD_C_EXTENSIONS", "1") == "1":
    try:
        build_cython_extensions()
    except Exception:
        if not allowed_to_fail:
            raise
