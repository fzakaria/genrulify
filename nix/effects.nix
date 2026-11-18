# Pushes to genrulify.cachix.org and pins main's build after nixbot builds
# main. `effects` is nixbot's effects library (nixbot.lib.effects):
# mkEffect gives the script CA certificates, and readSecretString reads
# the token from the secrets nixbot holds for this repository.
{
  pkgs,
  effects,
  packages,
}:
args:
let
  # What a user of the cache would otherwise build: the compiler, which
  # needs MLIR, and the site.
  pushed = with packages; [
    default
    site
  ];
  pinned = [ "default" ];
in
{
  onPush.default.outputs.effects.cachix =
    if args.branch == "main" then
      effects.mkEffect {
        name = "cachix-effect";
        inputs = [ pkgs.cachix ];
        secretsMap.cachix = "cachix";
        effectScript = ''
          CACHIX_AUTH_TOKEN=$(readSecretString cachix .token)
          export CACHIX_AUTH_TOKEN
          cachix push genrulify ${toString pushed}
          ${pkgs.lib.concatMapStrings (attr: ''
            cachix pin genrulify main-${attr} ${packages.${attr}} --keep-revisions 5
          '') pinned}
        '';
      }
    else
      null;
}
