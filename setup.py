import sys
from pathlib import Path

from setuptools import setup

sys.path.insert(0, str(Path(__file__).parent))
import build as build_module

build_module.build_cython_extensions()

setup()
