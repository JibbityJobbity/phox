{
  description = "Nix flake with GCC, CMake, Vulkan";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  # macOS gets Vulkan from MoltenVK, which only reports Vulkan 1.3 (required by Phox) from 1.3.0 on.
  # Pinned separately so the Linux shell (and the lavapipe used for goldens) does not move with it.
  inputs.nixpkgs-darwin.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs, nixpkgs-darwin }:
    let
      linux = "x86_64-linux";
      darwin = "aarch64-darwin";

      common = pkgs: with pkgs; [
        ninja
        cmake
        vulkan-loader
        vulkan-headers
        vulkan-tools # optional: useful for debugging Vulkan (vkcube etc.)
        glslang       # for shader compilation
        zlib
        SDL2
        SDL2.dev
        glm
        graphviz      # renders frame graphs dumped with --dump-graph: dot -Tsvg graph.dot -o graph.svg
      ];
    in {
      devShells.${linux}.default =
        let
          pkgs = import nixpkgs {
            system = linux;
            config.allowUnfree = true;
          };
        in pkgs.mkShell {
          name = "vulkan-dev-shell";

          packages = common pkgs ++ (with pkgs; [
            gcc
            xorg.libX11
            vscode
          ]);

          # Optional: setup environment variables for Vulkan SDK (some build systems expect this)
          shellHook = ''
            export VULKAN_SDK=${pkgs.vulkan-headers}/
            # volk dlopen()s libvulkan.so.1; mkShell doesn't put it on the loader path by default.
            export LD_LIBRARY_PATH=${pkgs.vulkan-loader}/lib''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
            # Makes the validation layer available (it is only enabled with --debug / PHOX_VALIDATION=1).
            export VK_ADD_LAYER_PATH=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d''${VK_ADD_LAYER_PATH:+:$VK_ADD_LAYER_PATH}
            # Mesa's CPU Vulkan driver, built against this shell's glibc (the system's drivers may not load
            # here). Use it with: VK_DRIVER_FILES=$PHOX_LAVAPIPE_ICD (this is what CI and the goldens use).
            export PHOX_LAVAPIPE_ICD=${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.x86_64.json
            echo "Vulkan SDK path set to $VULKAN_SDK"
          '';
        };

      # Builds with the default (clang) stdenv. There is no lavapipe here, so goldens stay a Linux job.
      devShells.${darwin}.default =
        let
          pkgs = import nixpkgs-darwin { system = darwin; };
        in pkgs.mkShell {
          name = "vulkan-dev-shell";

          packages = common pkgs ++ [ pkgs.moltenvk ];

          shellHook = ''
            export VULKAN_SDK=${pkgs.vulkan-headers}/
            # volk and SDL dlopen() libvulkan.dylib by name. SIP strips DYLD_* when a system binary
            # (/bin/sh, /usr/bin/env) starts the program, so run it directly or from this shell.
            export DYLD_LIBRARY_PATH=${pkgs.vulkan-loader}/lib''${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}
            # The loader finds MoltenVK (Vulkan on Metal) itself, through the XDG_DATA_DIRS mkShell sets.
            # Makes the validation layer available (it is only enabled with --debug / PHOX_VALIDATION=1).
            # A VK_LAYER_PATH from outside replaces the search, VK_ADD_LAYER_PATH included, so extend it.
            layers=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d
            if [ -n "''${VK_LAYER_PATH:-}" ]; then
              export VK_LAYER_PATH=$layers:$VK_LAYER_PATH
            else
              export VK_ADD_LAYER_PATH=$layers''${VK_ADD_LAYER_PATH:+:$VK_ADD_LAYER_PATH}
            fi
            unset layers
            echo "Vulkan SDK path set to $VULKAN_SDK"
          '';
        };
    };
}
