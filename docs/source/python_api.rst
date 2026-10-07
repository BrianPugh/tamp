.. _Python API:

==========
Python API
==========

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
                  Read data will be :obj:`str`.

                * Read-binary-mode (``"rb"``) will return a :py:class:`tamp.Decompressor`.
                  Read data will be :obj:`bytes`.

                * Write-text-mode (``"w"``) will return a :py:class:`tamp.TextCompressor`.
                  :obj:`str` must be provided to :meth:`~tamp.TextCompressor.write`.

                * Write-binary-mode (``"wb"``) will return a :py:class:`tamp.Compressor`.
                  :obj:`bytes` must be provided to :meth:`~tamp.Compressor.write`.
   :type mode: str
   :param kwargs: Passed along to class constructor.

   :return: File-like object for compressing/decompressing.

.. autofunction:: tamp.initialize_dictionary

    Initialize Dictionary.

    The character table used for seeding depends on the ``literal`` bit width:
    for ``literal=7`` or ``8``, common english text and markup characters are used;
    for ``literal=5`` or ``6``, common english letters (``" etaoinshrdlcumw"``)
    downshifted to the target bit width are used instead.

    For v1 backwards compatibility, pass ``literal=8`` (the default) when the
    ``extended`` header flag is not set.

    :param size:
        If a :obj:`bytearray`, will populate it with initial data.
        If an :obj:`int`, will allocate and initialize a bytearray of indicated size.
    :type size: Union[int, bytearray]
    :param literal:
        Number of literal bits (5-8). Selects the appropriate seed character table.
        Defaults to ``8``.
    :type literal: int

    :return: Initialized window dictionary.
    :rtype: bytearray

.. autoexception:: tamp.ExcessBitsError
   :exclude-members: args, add_note, with_traceback
