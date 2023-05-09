import shutil
from distutils.core import Distribution, Extension
from pathlib import Path

from Cython.Build import build_ext, cythonize

extension = Extension(
    "tamp._c",
    [
        "tamp/tamp.pyx",
        "tamp/_c_src/compressor.c",
        "tamp/_c_src/common.c",
    ],
    include_dirs=["tamp/_c_src/"],
    extra_compile_args=[
        "-O3",
        "-Werror",
        "-Wno-unreachable-code-fallthrough",  # https://github.com/cython/cython/issues/5041
    ],
)

ext_modules = cythonize([extension], include_path=extension.include_dirs, language_level=3)
dist = Distribution({"ext_modules": ext_modules})
cmd = build_ext(dist)
cmd.ensure_finalized()
cmd.run()

for output in cmd.get_outputs():
    output = Path(output)
    relative_extension = output.relative_to(cmd.build_lib)
    shutil.copyfile(output, relative_extension)
