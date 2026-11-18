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
      packages.${system} = rec {
        example = import ./nix/example.nix { inherit pkgs system; };
        default = import ./nix/package.nix { inherit pkgs; };
        site = import ./nix/site.nix {
          inherit pkgs;
          compiler = default;
        };
        serve = pkgs.writeShellScriptBin "serve" ''
          exec ${pkgs.darkhttpd}/bin/darkhttpd ${site} --addr 127.0.0.1 --port "''${1:-8136}" --default-mimetype text/plain
        '';

      };
      devShells.${system}.default = pkgs.mkShell {
        inherit (self.packages.${system}.default) SPLICE_BASH SPLICE_COREUTILS SPLICE_NIX;
        inputsFrom = [ self.packages.${system}.default ];
      };
      checks.${system} = { inherit (self.packages.${system}) default site; };
      formatter.${system} = pkgs.nixfmt-tree;
    };
}
