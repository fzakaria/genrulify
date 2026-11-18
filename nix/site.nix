# `nix build .#site` is the whole deployable tree, so the pages workflow
# only has to upload what nix built:
#
#   index.html, style.css, js.<hash>/   the explorer
#   data/                               one directory per recorded package,
#                                       written by tools/site-data.sh
{ pkgs }:
pkgs.runCommand "nixsplice-site" { } ''
  mkdir -p $out
  # The site directory alone, so a change to the compiler or a test does
  # not rebuild the site.
  cp -r ${../site}/. $out/
  chmod -R u+w $out

  # The footer names the store path serving the page.
  substituteInPlace $out/js/app.js --replace-fail "__STORE_PATH__" "$out"

  # Hash the module tree and rename it js.<hash>, so the served HTML and
  # every module it imports can never be a mismatched pair across deploys.
  hash=$(find $out/js -type f | LC_ALL=C sort |
    xargs sha256sum | sha256sum | cut -c1-12)
  mv $out/js "$out/js.$hash"
  substituteInPlace $out/index.html --replace-fail "js/app.js" "js.$hash/app.js"

  # The stylesheet is fetched by a fixed name, so its content hash rides
  # as a query string instead.
  css=$(sha256sum $out/style.css | cut -c1-12)
  substituteInPlace $out/index.html --replace-fail '"style.css"' "\"style.css?v=$css\""
''
