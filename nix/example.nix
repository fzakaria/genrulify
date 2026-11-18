{ pkgs, system }:
let
  dd = "${pkgs.coreutils}/bin/dd";
  inherit (import ./sources.nix) header body;
in
builtins.derivation {
  inherit system;
  name = "nixsplice-example";
  builder = "${pkgs.bash}/bin/bash";
  args = [
    "-eu"
    "-c"
    ''
      ${dd} if=${header} of=bundle status=none
      ${dd} if=${body} of=bundle oflag=append conv=notrunc status=none
      ${dd} if=bundle of="$out" status=none
    ''
  ];
}
