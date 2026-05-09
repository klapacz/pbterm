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

The default app build now links `libghostty-vt.a` and runs a tiny startup smoke test that creates and frees a Ghostty terminal. This is only a proof of integration; pbterm's renderer has not been replaced yet.

## Ghostty VT ARM proof build

The flake also contains a reproducible proof build for Ghostty's VT/terminal core:

```sh
nix build .#ghostty-vt-arm --out-link result-ghostty
```

This builds Ghostty with Zig `0.15.2` for `arm-linux-gnueabi`, disables SIMD, and installs:

```text
result-ghostty/lib/libghostty-vt.a
result-ghostty/include/ghostty/vt.h
result-ghostty/share/pkgconfig/libghostty-vt-static.pc
```

Ghostty's Zig dependencies are provided through upstream's generated `build.zig.zon.nix` and passed with `zig build --system`, so the build does not rely on a mutable Zig global cache or network access inside the Nix sandbox.

## Development shell

```sh
nix develop
cmake -S . -B build -DPOCKETBOOK_SDK="$POCKETBOOK_SDK" -DGHOSTTY_VT_ROOT="$GHOSTTY_VT_ROOT" -DCMAKE_BUILD_TYPE=Release
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
POCKETBOOK_SDK=/path/to/SDK-B288 GHOSTTY_VT_ROOT=/path/to/ghostty-vt-arm ./makearm.sh
# or
cmake -S . -B build -DPOCKETBOOK_SDK=/path/to/SDK-B288 -DGHOSTTY_VT_ROOT=/path/to/ghostty-vt-arm -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
