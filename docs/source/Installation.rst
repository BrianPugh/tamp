Installation
============
Tamp contains 2 implementations: a desktop cpython implementation that is optimized for readability, and a micropython implementation that is optimized for runtime performance.

Desktop Python
^^^^^^^^^^^^^^
The Tamp library and CLI requires Python ``>=3.8`` and can be installed via:

.. code-block:: bash

   pip install tamp

To install directly from github, you can run:

.. code-block:: bash

   python -m pip install git+https://github.com/BrianPugh/tamp.git

For development, its recommended to use Poetry:

.. code-block:: bash

   git clone https://github.com/BrianPugh/tamp.git
   cd tamp
   poetry install

MicroPython
^^^^^^^^^^^
For micropython use, there are 3 main files:

1. ``tamp/__init__.py`` - Always required.

2. ``tamp/decompressor_viper.py`` - Required for on-device decompression.

3. ``tamp/compressor_viper.py`` - Required for on-device compression.

For example, if on-device decompression isn't used, then do not include ``decompressor_viper.py``.
If manually installing, just copy these files to your microcontroller's ``/lib/tamp`` folder.

If using `Belay`_, tamp can be installed by adding the following to ``pyproject.toml``.

.. code-block:: toml

   [tool.belay.dependencies]
   tamp = [
      "https://github.com/BrianPugh/tamp/blob/main/tamp/__init__.py",
      "https://github.com/BrianPugh/tamp/blob/main/tamp/compressor_viper.py",
      "https://github.com/BrianPugh/tamp/blob/main/tamp/decompressor_viper.py",
   ]

.. _Belay: https://github.com/BrianPugh/belay
