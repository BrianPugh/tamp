Installation
============
Tamp contains 4 implementations:

1. A reference desktop CPython implementation that is optimized for readability (and **not** speed).

2. A Micropython Native Module implementation (fast).

3. A Micropython Viper implementation (not recommended, please use Native Module).

4. A C implementation (with python bindings) for accelerated desktop use and to be used in C projects (very fast).

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

Native Module
-------------
Tamp provides pre-compiled `native modules` that are easy to install, are small, and are incredibly fast.

Download the appropriate ``.mpy`` file from the `release page`_.

   * Match the micropython version.

   * Match the architecture to the microcontroller (e.g. ``armv6m`` for a pi pico).

Rename the file to ``tamp.mpy`` and transfer it to your board. If using `Belay`_, tamp can be installed by adding the following to ``pyproject.toml``.

.. code-block:: toml

   [tool.belay.dependencies]
   tamp = "https://github.com/BrianPugh/tamp/releases/download/v1.6.0/tamp-1.6.0-mpy1.23-armv6m.mpy"

Viper
-----
**NOT RECOMMENDED, PLEASE USE NATIVE MODULE**

For micropython use, there are 3 main files:

1. ``tamp/__init__.py`` - Always required.

2. ``tamp/decompressor_viper.py`` - Required for on-device decompression.

3. ``tamp/compressor_viper.py`` - Required for on-device compression.

For example, if on-device decompression isn't used, then do not include ``decompressor_viper.py``.
If manually installing, just copy these files to your microcontroller's ``/lib/tamp`` folder.

If using `mip`_, tamp can be installed by specifying the appropriate ``package-*.json`` file.

.. code-block:: bash

   mip install github:brianpugh/tamp  # Defaults to package.json: Compressor & Decompressor
   mip install github:brianpugh/tamp/package-compressor.json  # Compressor only
   mip install github:brianpugh/tamp/package-decompressor.json  # Decompressor only

If using `Belay`_, tamp can be installed by adding the following to ``pyproject.toml``.

.. code-block:: toml

   [tool.belay.dependencies]
   tamp = [
      "https://github.com/BrianPugh/tamp/blob/main/tamp/__init__.py",
      "https://github.com/BrianPugh/tamp/blob/main/tamp/compressor_viper.py",
      "https://github.com/BrianPugh/tamp/blob/main/tamp/decompressor_viper.py",
   ]

C
^

Copy the ``tamp/_c_src/tamp`` folder into your project.
For more information, see :ref:`C Library`.

.. _mip: https://docs.micropython.org/en/latest/reference/packages.html#installing-packages-with-mip
.. _Belay: https://github.com/BrianPugh/belay
.. _release page: https://github.com/BrianPugh/tamp/releases
