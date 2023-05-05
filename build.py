import shutil
from distutils.core import Distribution, Extension
from pathlib import Path

from Cython.Build import build_ext, cythonize

cython_dir = Path("tamp/_c_src/")
extension = Extension(
    "tamp._c",
    [
        str(cython_dir / "tamp.pyx"),
    ],
    # extra_compile_args=["-O3"],
)

ext_modules = cythonize([extension], include_path=[cython_dir], language_level=3)
dist = Distribution({"ext_modules": ext_modules})
cmd = build_ext(dist)
cmd.ensure_finalized()
cmd.run()

for output in cmd.get_outputs():
    output = Path(output)
    relative_extension = output.relative_to(cmd.build_lib)
    shutil.copyfile(output, relative_extension)
