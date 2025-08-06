# ESP-IDF Component
The directory contains code for the [esp-optimized Tamp esp-idf component](https://components.espressif.com/components/brianpugh/tamp).

See the [esp-tamp-demo repo](https://github.com/BrianPugh/esp-tamp-demo) for running this code on an espressif chip like the esp32.

# Building Component
The following will produce a `dist/` directory containing a packaged esp-idf component that is ready to be uploaded to the esp registry.
```
cd tamp  # i.e. "espidf/tamp" from the root of this repo
make
```

Also see the `.github/workflows/esp_upload_component.yml` workflow.
