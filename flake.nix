{
  description = "Compile a recorded Nix build into Bazel and Buck2 genrules";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # nixbot's effects library, for the Cachix effect (nix/effects.nix)
    nixbot.url = "github:Mic92/nixbot";
    nixbot.inputs.nixpkgs.follows = "nixpkgs";
  };

  # CI pushes every build of main to this cache, so
  # `nix run github:fzakaria/genrulify` downloads the compiler instead of
  # building it against MLIR. Nix asks before using a flake's substituter
  # unless the user is trusted.
  nixConfig = {
    extra-substituters = [ "https://genrulify.cachix.org" ];
    extra-trusted-public-keys = [
      "genrulify.cachix.org-1:Evm/4piGJrTklr4dacIeXtT2VKdcZOQUeGW35zq8Cgk="
    ];
  };

  outputs =
    {
      self,
      nixpkgs,
      nixbot,
    }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in
    {
      packages.${system} = rec {
        example = import ./nix/example.nix { inherit pkgs system; };
        flaky = import ./nix/flaky.nix { inherit pkgs system; };
        default = import ./nix/package.nix { inherit pkgs; };

        # the tree GitHub Pages deploys and `nix run .#serve` tests locally
        site = import ./nix/site.nix {
          inherit pkgs;
          revision = self.rev or null;
        };
      };

      # Serve the built site, exactly the tree pages deploys.
      # `nix run .#serve -- <port>` overrides the default 8139. no-store
      # because store paths carry a 1970 mtime, and a 304 would keep a
      # previous build's JS forever. $GENRULIFY_SITE serves a checkout's
      # site/ directory instead, for iterating without a rebuild.
      apps.${system}.serve = {
        type = "app";
        program = "${pkgs.writeShellScript "serve-site" ''
          exec ${pkgs.python3}/bin/python3 - "''${1:-8139}" <<'EOF'
          import http.server, os, sys, urllib.parse

          SITE = os.environ.get("GENRULIFY_SITE") or "${self.packages.${system}.site}"

          class Handler(http.server.SimpleHTTPRequestHandler):
              def translate_path(self, path):
                  path = urllib.parse.urlparse(path).path
                  if path == "/":
                      path = "/index.html"
                  return os.path.join(SITE, path.lstrip("/"))

              def end_headers(self):
                  self.send_header("Cache-Control", "no-store")
                  super().end_headers()

          http.server.SimpleHTTPRequestHandler.extensions_map[".js"] = "text/javascript"
          port = int(sys.argv[1])
          print(f"serving on http://127.0.0.1:{port}/ (site={SITE})", flush=True)
          http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
          EOF
        ''}";
      };
      devShells.${system}.default = pkgs.mkShell {
        inherit (self.packages.${system}.default)
          SPLICE_BASH
          SPLICE_COREUTILS
          SPLICE_NIX
          SPLICE_BWRAP
          SPLICE_TAR
          SPLICE_STRACE
          ;
        inputsFrom = [ self.packages.${system}.default ];
      };
      checks.${system} = { inherit (self.packages.${system}) default site; };
      formatter.${system} = pkgs.nixfmt-tree;

      # nixbot's effects (nix/effects.nix)
      herculesCI = import ./nix/effects.nix {
        inherit pkgs;
        effects = nixbot.lib.effects { inherit pkgs; };
        packages = self.packages.${system};
      };
    };
}
