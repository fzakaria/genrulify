{
  description = "Compile a recorded Nix build into Bazel and Buck2 genrules";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system} = rec {
        example = import ./nix/example.nix { inherit pkgs system; };
        default = import ./nix/package.nix { inherit pkgs; };
      };
      devShells.${system}.default = pkgs.mkShell {
        inherit (self.packages.${system}.default)
          SPLICE_BASH
          SPLICE_COREUTILS
          SPLICE_NIX
          SPLICE_BWRAP
          SPLICE_TAR
          ;
        inputsFrom = [ self.packages.${system}.default ];
      };
      checks.${system} = { inherit (self.packages.${system}) default; };
      formatter.${system} = pkgs.nixfmt-tree;
    };
}
