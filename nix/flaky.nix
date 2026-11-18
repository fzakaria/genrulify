# A derivation with one step whose output differs between runs. It exists
# to exercise what a conversion does with a rule that does not reproduce:
# `shuf` picks "common" four times in five, so it can match the recording
# in both replay checks and still differ in a later build.
{ pkgs, system }:
let
  bin = "${pkgs.coreutils}/bin";
in
builtins.derivation {
  inherit system;
  name = "nixsplice-flaky";
  builder = "${pkgs.bash}/bin/bash";
  args = [
    "-eu"
    "-c"
    ''
      ${bin}/mkdir "$out"
      ${bin}/shuf -n 1 -e common common common common rare > "$out/coin"
      ${bin}/cp "$out/coin" "$out/copy"
      ${bin}/printf 'stable\n' > "$out/stable"
    ''
  ];
}
