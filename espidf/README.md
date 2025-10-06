# ESP-IDF Component

The directory contains code for the
[esp-optimized Tamp esp-idf component](https://components.espressif.com/components/brianpugh/tamp).

See the [esp-tamp-demo repo](https://github.com/BrianPugh/esp-tamp-demo) for
running this code on an espressif chip like the esp32.

## Building Component

The following commands (run from the repository root) will prepare and package
the ESP-IDF component:

```bash
# Copy C source files to the ESP-IDF component directory
make esp-idf-copy-sources

# Build the component package (creates dist/ directory)
make esp-idf-component-build

# Clean component artifacts
make esp-idf-component-clean
```

The `esp-idf-component-build` target produces a `dist/` directory in
`espidf/tamp/` containing a packaged ESP-IDF component ready to be uploaded to
the esp registry.

Also see the `.github/workflows/esp_upload_component.yml` workflow.
