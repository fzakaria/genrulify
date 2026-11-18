{ pkgs, compiler }:
pkgs.runCommand "nixsplice-site"
  {
    fixtureSources = builtins.attrValues (import ./sources.nix);
    nativeBuildInputs = [ compiler ];
  }
  ''
    mkdir -p $out/artifacts
    cp ${../site}/style.css $out/
    cp ${../tests/data/run.json} $out/artifacts/run.json
    cp ${../tests/data/events.jsonl} $out/artifacts/events.jsonl
    cp ${./example.nix} $out/artifacts/example.nix
    cp ${./sources.nix} $out/artifacts/sources.nix
    cd $out/artifacts
    nixsplice import ${../tests/data/derivation.json} events.jsonl > recorded.mlir
    nixsplice opt recorded.mlir > folded.mlir
    for graph in recorded folded; do
      nixsplice emit $graph.mlir "$TMPDIR/$graph"
      cp "$TMPDIR/$graph/BUILD.bazel" "$graph.BUILD"
      cp "$TMPDIR/$graph/BUCK" "$graph.BUCK"
    done

    # Show the generated action inline and link the complete build files.
    awk '/^genrule\(/ { on=1 } on {print} on && /^\)/ {exit}' folded.BUILD > "$TMPDIR/bazel.rule"
    awk '/^genrule\(/ { on=1 } on {print} on && /^\)/ {exit}' folded.BUCK > "$TMPDIR/buck.rule"
    cp {recorded,folded}.mlir "$TMPDIR/"
    awk -v dir="$TMPDIR" '
      /@@[a-z]+\.(mlir|rule)@@/ {
        match($0, /@@[a-z]+\.(mlir|rule)@@/)
        file = dir "/" substr($0, RSTART + 2, RLENGTH - 4)
        print substr($0, 1, RSTART - 1)
        while ((getline code < file) > 0) {
          gsub(/\&/, "\\\&amp;", code)
          gsub(/</, "\\\&lt;", code)
          print code
        }
        close(file)
        print substr($0, RSTART + RLENGTH)
        next
      } { print }
    ' ${../site}/index.html > $out/index.html
  ''
