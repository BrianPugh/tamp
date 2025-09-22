====
Rust
====

Tamp has **unofficial** Rust bindings available through the `tamp-rs` crate, which provides both safe Rust bindings and low-level FFI access to the C library.

Over time, these bindings **may** become integrated with the official Tamp repo.

Documentation
=============

* `tamp crate documentation <https://docs.rs/tamp/latest/tamp/index.html>`_
* `tamp-sys FFI documentation <https://docs.rs/tamp-sys/latest/tamp_sys/>`_
* `GitHub repository <https://github.com/tmpfs/tamp-rs>`_

Installation
============

Add the following to your ``Cargo.toml``:

.. code-block:: toml

    [dependencies]
    tamp = "*"  # Use latest version

Or using cargo from the command line:

.. code-block:: bash

    cargo add tamp

Example Usage
=============

Here's a simple example showing how to compress and decompress data using the Rust bindings:

.. code-block:: rust

    use tamp::{Compressor, Decompressor, Config};

    // Use 1K buffer size (must match 2^window_bits)
    const WINDOW_SIZE: usize = 1 << 10;

    fn main() -> Result<(), tamp::Error> {
        // Original data to compress - repetitive text compresses better
        let input = b"I scream, you scream, we all scream for ice cream!";

        // Create configuration with window_bits=10 (2^10 = 1024)
        let config = Config {
            window_bits: 10,     // 2^10 = 1024 bytes
            literal_bits: 7,     // Use 7-bit literals for text (ASCII) compression
            ..Default::default()
        };

        // Instantiate the Compressor + Buffers
        let mut compressor: Compressor<WINDOW_SIZE> = Compressor::new(config.clone())?;
        let mut output_buffer = vec![0u8; 1024];
        let mut compressed = Vec::new();

        // Compress chunk and flush (without end token)
        let (_, written) = compressor.compress_chunk(input, &mut output_buffer)?;
        compressed.extend_from_slice(&output_buffer[..written]);
        let written = compressor.flush(&mut output_buffer, false)?;
        compressed.extend_from_slice(&output_buffer[..written]);

        println!("Compressed {} bytes to {} bytes", input.len(), compressed.len());

        // Decompress the data - create decompressor from header
        let (mut decompressor, header_size): (Decompressor<WINDOW_SIZE>, usize) =
            Decompressor::from_header(&compressed)?;

        let mut decompressed = vec![0u8; 1024];
        let (_, written) = decompressor.decompress_chunk(
            &compressed[header_size..],  // Skip header bytes
            &mut decompressed
        )?;

        println!("Decompressed back to {} bytes", written);
        assert_eq!(input, &decompressed[..written]);

        Ok(())
    }
