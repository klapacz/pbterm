# Building pbterm

This project now has a reproducible Nix-based PocketBook build pipeline using the PocketBook `SDK_6.3.0` `SDK-B288` target.

## Nix build

```sh
nix build
```

The built app is available at:

```text
result/bin/pbterm.app
```

## Development shell

```sh
nix develop
cmake -S . -B build -DPOCKETBOOK_SDK="$POCKETBOOK_SDK" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or use the wrapper script:

```sh
nix develop
./makearm.sh
```

## Local SDK build without Nix

If you have the SDK checked out beside this repository, the default SDK path is:

```text
../SDK/SDK_6.3.0/SDK-B288
```

Override it with either an environment variable or a CMake option:

```sh
POCKETBOOK_SDK=/path/to/SDK-B288 ./makearm.sh
# or
cmake -S . -B build -DPOCKETBOOK_SDK=/path/to/SDK-B288 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
