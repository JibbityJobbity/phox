{
  description = "Nix flake with GCC, CMake, Vulkan";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };
    in {
      devShells.${system}.default = pkgs.mkShell {
        name = "vulkan-dev-shell";

        packages = with pkgs; [
          gcc
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
          xorg.libX11
          vscode
        ];

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
    };
}
