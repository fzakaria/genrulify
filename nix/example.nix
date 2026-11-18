{ pkgs, system }:
let
  dd = "${pkgs.coreutils}/bin/dd";
  header = builtins.toFile "header.txt" "Nixsplice\n";
  body = builtins.toFile "body.txt" "one file, two versions\n";
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
