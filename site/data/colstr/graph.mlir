module {
  %0 = splice.literal "sandbox" at 717 -> [
      #splice.entry<"/build" dir 700 mtime 1>
  ]
  %1:3 = splice.exec "cp" at 2004(%0) -> [
      #splice.entry<"/build/source" dir 555 mtime 1>,
      #splice.entry<"/build/source/LICENSE" file 444 mtime 1 "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499">,
      #splice.entry<"/build/source/main.cpp" file 444 mtime 1 "c60e9167c6054107aa3267b87fea18c163f7f87da25e2c17da1b7eb763ced861">
  ]
  %2:3 = splice.exec "chmod" at 2030(%0, %1#0, %1#1, %1#2) -> [
      #splice.entry<"/build/source" dir 755 mtime 1>,
      #splice.entry<"/build/source/LICENSE" file 644 mtime 1 "a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499">,
      #splice.entry<"/build/source/main.cpp" file 644 mtime 1 "c60e9167c6054107aa3267b87fea18c163f7f87da25e2c17da1b7eb763ced861">
  ]
  %3 = splice.exec "c++" at 2607(%0, %2#0, %2#2) -> [
      #splice.entry<"/build/source/a.out" file 755 mtime 1 "bd25c4990c4631046d50dc0a711b878941fb24bc27e31e6d34512633b9615812">
  ]
  %4:3 = splice.exec "install" at 3057(%0, %2#0, %3) -> [
      #splice.entry<"/nix/store/s7lfxf6nljg66na31h9ay8qj8adiwdxs-colstr-1.0.0" dir 755 mtime 1>,
      #splice.entry<"/nix/store/s7lfxf6nljg66na31h9ay8qj8adiwdxs-colstr-1.0.0/bin" dir 755 mtime 1>,
      #splice.entry<"/nix/store/s7lfxf6nljg66na31h9ay8qj8adiwdxs-colstr-1.0.0/bin/colstr" file 755 mtime 1 "bd25c4990c4631046d50dc0a711b878941fb24bc27e31e6d34512633b9615812">
  ]
  %5 = splice.exec "find|patchelf" at 3276(%0, %2#0, %4#0, %4#1, %4#2) -> [
      #splice.entry<"/nix/store/s7lfxf6nljg66na31h9ay8qj8adiwdxs-colstr-1.0.0/bin/colstr" file 755 mtime 1 "c764b687f2f8d20e3a004571038143609f2e7e6af9a302bf302a9788dcdc08a8">
  ]
  %6 = splice.literal "mktemp" at 3614 demoted -> [
      #splice.entry<"/build/striperr.pG5kf7" file 600 mtime 1 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">
  ]
  %7 = splice.exec "find|sort|cut|xargs" at 3650(%0, %2#0, %6, %4#0, %4#1, %5) -> [
      #splice.entry<"/nix/store/s7lfxf6nljg66na31h9ay8qj8adiwdxs-colstr-1.0.0/bin/colstr" file 755 mtime 1 "99d3ec89bbef74dd78b0ba3ae753696ee3153105c4d34b05dd907cdfb0d9c2c7">
  ]
  splice.output "out" "/nix/store/s7lfxf6nljg66na31h9ay8qj8adiwdxs-colstr-1.0.0"(%4#0, %4#1, %7)
}

