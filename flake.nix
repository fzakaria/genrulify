{
  description = "Compile a recorded Nix build into versioned file actions";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system} = {
        example = import ./nix/example.nix { inherit pkgs system; };
        default = import ./nix/package.nix { inherit pkgs; };
      };
      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
      };
      formatter.${system} = pkgs.nixfmt-tree;
    };
}
