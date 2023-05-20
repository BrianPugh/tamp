import shutil
from distutils.core import Distribution, Extension
from pathlib import Path

from Cython.Build import build_ext, cythonize

extensions = [
    Extension(
        "tamp._c_compressor",
        [
            "tamp/_c_compressor.pyx",
            "tamp/_c_src/tamp/compressor.c",
            "tamp/_c_src/tamp/common.c",
        ],
        include_dirs=["tamp/_c_src/", "tamp/"],
        extra_compile_args=[
            "-O3",
            "-Werror",
            "-Wno-unreachable-code-fallthrough",  # https://github.com/cython/cython/issues/5041
        ],
    ),
]

include_dirs = set()
for extension in extensions:
    include_dirs.update(extension.include_dirs)
include_dirs = list(include_dirs)

ext_modules = cythonize(extensions, include_path=include_dirs, language_level=3)
dist = Distribution({"ext_modules": ext_modules})
cmd = build_ext(dist)
cmd.ensure_finalized()
cmd.run()

for output in cmd.get_outputs():
    output = Path(output)
    relative_extension = output.relative_to(cmd.build_lib)
    shutil.copyfile(output, relative_extension)
