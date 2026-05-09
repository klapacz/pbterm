{
  description = "Nix build for PocketBook pbterm";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";

    # Ghostty uses Zig 0.15.x. Use the same overlay family as upstream
    # Ghostty instead of hand-packaging a Zig binary in this flake.
    zig-overlay.url = "github:mitchellh/zig-overlay";
  };

  outputs = { self, nixpkgs, zig-overlay }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        overlays = [
          (_final: _prev: {
            zig_0_15 = zig-overlay.packages.${system}."0.15.2";
          })
        ];
      };

      sdkSrc = pkgs.fetchgit {
        url = "https://github.com/pocketbook/SDK_6.3.0.git";
        rev = "23eb32c3a011a1df4ce3d8f22150dcdd34cbc75a";
        hash = "sha256-3SQHEL6D3oY57Z8CTkcSniO/K4aQ7TrT3xupc9EXvzM=";
        sparseCheckout = [ "SDK-B288" ];
      };

      pocketbookSdk = pkgs.runCommand "pocketbook-sdk-6.3.0-B288" {
        nativeBuildInputs = [ pkgs.file pkgs.patchelf ];
      } ''
        cp -R --no-preserve=mode,ownership ${sdkSrc}/SDK-B288 $out
        chmod -R u+w $out

        # The SDK compiler frontends are symlinks to toolchain-wrapper. In a
        # Nix build sandbox the wrapper resolves symlinked argv[0] poorly, so
        # replace those symlinks with real wrapper copies.
        for f in $out/usr/bin/arm-* $out/usr/bin/clang $out/usr/bin/clang++; do
          if [ -L "$f" ] && [ "$(readlink "$f")" = "toolchain-wrapper" ]; then
            rm "$f"
            cp $out/usr/bin/toolchain-wrapper "$f"
            chmod +x "$f"
          fi
        done

        interp=${pkgs.glibc}/lib/ld-linux-x86-64.so.2
        rpath=${pkgs.lib.makeLibraryPath [ pkgs.zlib pkgs.ncurses5 pkgs.glibc pkgs.stdenv.cc.cc.lib ]}
        while IFS= read -r f; do
          if file "$f" | grep -q 'ELF .*x86-64'; then
            chmod +x "$f"
            patchelf --set-interpreter "$interp" "$f" 2>/dev/null || true
            patchelf --set-rpath "$rpath" "$f" 2>/dev/null || true
          fi
        done < <(find $out -type f)
      '';

      ghosttyRev = "063ac3ecc5adae6360ae2044dc54e7a68c64f3a1";
      ghosttyShortRev = builtins.substring 0 7 ghosttyRev;

      ghosttySrc = pkgs.fetchFromGitHub {
        owner = "ghostty-org";
        repo = "ghostty";
        rev = ghosttyRev;
        hash = "sha256-LfvUpnZvX62E+LIRUn2TaJgGFbBaxpeHO5OUBIVg5SY=";
      };

      # Ghostty upstream already vendors its build.zig.zon through zon2nix.
      # Import that generated Nix expression and pass it to Zig with --system
      # so the build is deterministic and never tries to populate Zig's global
      # cache from the network inside the sandbox.
      ghosttyZigDeps = pkgs.callPackage "${ghosttySrc}/build.zig.zon.nix" {
        name = "ghostty-zig-deps-${ghosttyShortRev}";
        zig_0_15 = pkgs.zig_0_15;

        # Zig 0.15's build runner has a path-depth bug when dependency entries
        # are symlinks. Ghostty's own nix/libghostty-vt.nix works around this by
        # replacing linkFarm with a copy-farm; keep the same workaround here.
        linkFarm = name: entries:
          pkgs.runCommand name { } ''
            mkdir -p $out
            ${pkgs.lib.concatMapStringsSep "\n" (e: ''
              cp -rL ${e.path} $out/${e.name}
            '') entries}
          '';
      };
    in {
      packages.${system} = rec {
        sdk = pocketbookSdk;
        zig-0-15-2 = pkgs.zig_0_15;

        ghostty-vt-arm = pkgs.stdenv.mkDerivation {
          pname = "ghostty-vt-arm";
          version = "0.1.0-${ghosttyShortRev}";
          src = ghosttySrc;
          nativeBuildInputs = [ pkgs.git pkgs.pkg-config pkgs.zig_0_15 ];
          dontConfigure = true;
          dontSetZigDefaultFlags = true;
          buildPhase = ''
            runHook preBuild
            export HOME=$TMPDIR
            zig build \
              --system ${ghosttyZigDeps} \
              -Demit-lib-vt=true \
              -Dlib-version-string=0.1.0-dev+${ghosttyShortRev}-nix \
              -Dcpu=baseline \
              -Dtarget=arm-linux-gnueabi \
              -Dsimd=false \
              -Doptimize=ReleaseFast \
              -Dapp-runtime=none \
              --cache-dir "$TMPDIR/zig-cache" \
              --global-cache-dir "$TMPDIR/zig-global-cache"
            runHook postBuild
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/lib $out/include $out/share/pkgconfig
            cp zig-out/lib/libghostty-vt.a $out/lib/
            cp -R zig-out/include/ghostty $out/include/
            if [ -d zig-out/share/pkgconfig ]; then
              cp zig-out/share/pkgconfig/*.pc $out/share/pkgconfig/
            fi
            runHook postInstall
          '';
        };

        default = pkgs.stdenv.mkDerivation {
          pname = "pbterm";
          version = "0.0.1";
          src = self;
          nativeBuildInputs = [ pkgs.cmake pkgs.gnumake ];
          dontFixCmake = true;
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DPOCKETBOOK_SDK=${pocketbookSdk}"
            "-DGHOSTTY_VT_ROOT=${ghostty-vt-arm}"
          ];
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp bin/pbterm.app $out/bin/
            runHook postInstall
          '';
        };
      };

      checks.${system} = {
        terminal-bytes = pkgs.stdenv.mkDerivation {
          pname = "terminal-bytes-test";
          version = "0.0.1";
          src = self;
          dontConfigure = true;
          buildPhase = ''
            runHook preBuild
            $CXX -std=c++17 \
              -I$src/src \
              $src/src/TerminalBytes.cpp \
              $src/tests/TerminalBytesTest.cpp \
              -o terminal-bytes-test
            runHook postBuild
          '';
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            ./terminal-bytes-test
            runHook postCheck
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp terminal-bytes-test $out/bin/
            runHook postInstall
          '';
        };

        bluetooth-keyboard-discovery = pkgs.stdenv.mkDerivation {
          pname = "bluetooth-keyboard-discovery-test";
          version = "0.0.1";
          src = self;
          dontConfigure = true;
          buildPhase = ''
            runHook preBuild
            $CXX -std=c++17 \
              -I$src/src \
              $src/src/BluetoothKeyboardDiscovery.cpp \
              $src/tests/BluetoothKeyboardDiscoveryTest.cpp \
              -o bluetooth-keyboard-discovery-test
            runHook postBuild
          '';
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            ./bluetooth-keyboard-discovery-test
            runHook postCheck
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp bluetooth-keyboard-discovery-test $out/bin/
            runHook postInstall
          '';
        };

        terminal-screen-state = pkgs.stdenv.mkDerivation {
          pname = "terminal-screen-state-test";
          version = "0.0.1";
          src = self;
          dontConfigure = true;
          buildPhase = ''
            runHook preBuild
            $CXX -std=c++17 \
              -I$src/src \
              $src/tests/TerminalScreenStateTest.cpp \
              -o terminal-screen-state-test
            runHook postBuild
          '';
          doCheck = true;
          checkPhase = ''
            runHook preCheck
            ./terminal-screen-state-test
            runHook postCheck
          '';
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp terminal-screen-state-test $out/bin/
            runHook postInstall
          '';
        };
      };

      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = [ pkgs.cmake pkgs.gnumake pkgs.ninja pkgs.pkg-config pkgs.zig_0_15 ];
        POCKETBOOK_SDK = pocketbookSdk;
        SDK_ROOT = pocketbookSdk;
        GHOSTTY_SRC = ghosttySrc;
        GHOSTTY_ZIG_DEPS = ghosttyZigDeps;
        GHOSTTY_VT_ROOT = self.packages.${system}.ghostty-vt-arm;
        shellHook = ''
          echo "PocketBook SDK: ${pocketbookSdk}"
          echo "Ghostty source: ${ghosttySrc}"
          echo "Ghostty Zig deps: ${ghosttyZigDeps}"
          echo "Configure: cmake -S . -B build -DPOCKETBOOK_SDK=$POCKETBOOK_SDK -DGHOSTTY_VT_ROOT=$GHOSTTY_VT_ROOT -DCMAKE_BUILD_TYPE=Release"
          echo "Build:     cmake --build build"
          echo "Nix:       nix build"
          echo "Ghostty:   nix build .#ghostty-vt-arm"
        '';
      };
    };
}
