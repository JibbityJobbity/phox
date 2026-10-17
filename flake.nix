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
          xorg.libX11
          vscode
        ];

        # Optional: setup environment variables for Vulkan SDK (some build systems expect this)
        shellHook = ''
          export VULKAN_SDK=${pkgs.vulkan-headers}/
          echo "Vulkan SDK path set to $VULKAN_SDK"
        '';
      };
    };
}
