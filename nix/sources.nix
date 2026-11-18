{
  header = builtins.toFile "header.txt" "Genrulify\n";
  body = builtins.toFile "body.txt" "one file, two versions\n";
}
