API
===

.. autoclass:: tamp.Compressor

.. autoclass:: tamp.TextCompressor

.. autofunction:: tamp.compress

.. autoclass:: tamp.Decompressor

.. autoclass:: tamp.TextDecompressor

.. autofunction:: tamp.decompress

.. py:function:: tamp.open(f, mode="rb", **kwargs)

   Opens a file for compressing/decompressing.

   Example usage::

       with tamp.open("file.tamp", "w") as f:
           # Opens a compressor in text-mode
           f.write("example text")

       with tamp.open("file.tamp", "r") as f:
           # Opens a decompressor in text-mode
           assert f.read() == "example text"

   :param f: PathLike object to open.
   :type f: Union[str, Path]
   :param mode: Opening mode. Must be some combination of ``{"r", "w", "b"}``.

                * Read-text-mode (``"r"``) will return a :py:class:`tamp.TextDecompressor`.
                  Read data will be ``str``.

                * Read-binary-mode (``"rb"``) will return a :py:class:`tamp.Decompressor`.
                  Read data will be ``bytes``.

                * Write-text-mode (``"w"``) will return a :py:class:`tamp.TextCompressor`.
                  ``str`` must be provided to ``write``.

                * Write-binary-mode (``"wb"``) will return a :py:class:`tamp.Compressor`.
                  ``bytes`` must be provided to ``write``.
   :type mode: str
   :return: File-like object for compressing/decompressing.

.. autoexception:: tamp.ExcessBitsError
   :exclude-members: args, add_note, with_traceback
