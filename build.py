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

    if profile:
        print("Setting profiling configuration.")
        directive_defaults = get_directive_defaults()
        directive_defaults["linetrace"] = True
        directive_defaults["binding"] = True

        define_macros.append(("CYTHON_TRACE", "1"))

    if os.name == "nt":  # Windows
        extra_compile_args = [
            "/O2",
        ]
    else:  # UNIX-based systems
        extra_compile_args = [
            "-O3",
            "-Werror",
            "-Wno-unreachable-code-fallthrough",
            "-Wno-deprecated-declarations",
            "-Wno-parentheses-equality",
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

    ext_modules = cythonize(extensions, include_path=include_dirs, language_level=3, annotate=True)
    dist = Distribution({"ext_modules": ext_modules})
    cmd = build_ext(dist)
    cmd.ensure_finalized()
    cmd.run()

    for output in cmd.get_outputs():
        output = Path(output)
        relative_extension = output.relative_to(cmd.build_lib)
        shutil.copyfile(output, relative_extension)


try:
    build_cython_extensions()
except Exception:
    if not allowed_to_fail:
        raise
