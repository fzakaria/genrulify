{
  header = builtins.toFile "header.txt" "Nixsplice\n";
  body = builtins.toFile "body.txt" "one file, two versions\n";
}
