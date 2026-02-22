# Tamp Website

Browser-based compression/decompression tool using Tamp's WebAssembly build.

## Prerequisites

The WASM build must exist in `../wasm/dist/` before building the website. See
`../wasm/` for build instructions.

## Development

```bash
npm install
npm run serve        # Dev server with hot reload
npm run build        # Production build to ../build/pages-deploy/
npm run build:dev    # Development build
```

Or from the project root:

```bash
make website-serve   # Install deps + dev server
make website-build   # Install deps + production build
make website-clean   # Remove build artifacts and node_modules
```
