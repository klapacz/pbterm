{
  description = "Nix build for PocketBook pbterm";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.05";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

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
    in {
      packages.${system} = rec {
        sdk = pocketbookSdk;
        default = pkgs.stdenv.mkDerivation {
          pname = "pbterm";
          version = "0.0.1";
          src = self;
          nativeBuildInputs = [ pkgs.cmake pkgs.gnumake ];
          dontFixCmake = true;
          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DPOCKETBOOK_SDK=${pocketbookSdk}"
          ];
          installPhase = ''
            runHook preInstall
            mkdir -p $out/bin
            cp bin/pbterm.app $out/bin/
            runHook postInstall
          '';
        };
      };

      devShells.${system}.default = pkgs.mkShell {
        nativeBuildInputs = [ pkgs.cmake pkgs.gnumake pkgs.ninja pkgs.pkg-config pkgs.zig ];
        POCKETBOOK_SDK = pocketbookSdk;
        SDK_ROOT = pocketbookSdk;
        shellHook = ''
          echo "PocketBook SDK: ${pocketbookSdk}"
          echo "Configure: cmake -S . -B build -DPOCKETBOOK_SDK=$POCKETBOOK_SDK -DCMAKE_BUILD_TYPE=Release"
          echo "Build:     cmake --build build"
          echo "Nix:       nix build"
        '';
      };
    };
}
