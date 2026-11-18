module {
  %0 = splice.literal "sandbox" at 716 -> [
      #splice.entry<"/build" dir 700 mtime 1>
  ]
  %1:11 = splice.exec "cp" at 2098(%0) -> [
      #splice.entry<"/build/source" dir 555 mtime 1>,
      #splice.entry<"/build/source/.gitignore" file 444 mtime 1 "b6c49994d4ac8e9c963f86fbbcbdc602e09f4fcea204b4cc1e2467e271a0f1d4">,
      #splice.entry<"/build/source/LICENSE" file 444 mtime 1 "72c7d262e2c9ddf019edc90af852a01a410338efff4e75e1c005b77ada4be7c4">,
      #splice.entry<"/build/source/Makefile" file 444 mtime 1 "dc8a4358768ef0bdf20e064cd5203ac5c67fb5e283acd5e2fcb0fae9b360f517">,
      #splice.entry<"/build/source/README.ja.md" file 444 mtime 1 "2a01b6b7d7fb8e658a22b0c80633f8bf3da3bed5cb1a19c347bcf8c3e71a5adc">,
      #splice.entry<"/build/source/README.md" file 444 mtime 1 "2e602db77ec71fc2e9fa75f85c5d1a33588cb613227df2d6e8d02eff016dc7f4">,
      #splice.entry<"/build/source/demo.gif" file 444 mtime 1 "cd29dd2d45e941679a71762e1fdd315e040034cfd9c2849d2bf60ae3e7f2be6b">,
      #splice.entry<"/build/source/sl.1" file 444 mtime 1 "a598abda4950f6dfca0351cacf776267da065cfe67c540ec2a21ed4039a334f7">,
      #splice.entry<"/build/source/sl.1.ja" file 444 mtime 1 "e516ffe42b2b0993e5dbfcf216314342b10a2f5a2eacfa7d9d5fc6bfa2ba0581">,
      #splice.entry<"/build/source/sl.c" file 444 mtime 1 "bb61dbb2c20ba279cb96e2c21c0a5ad8f8a6bb0b74bce7b4268e8d1434138bb1">,
      #splice.entry<"/build/source/sl.h" file 444 mtime 1 "02847f3f0a00fb0f8d5ccb493ae0f00dc8617a8c416c9a4ecd1b5a0d0ee7c4d7">
  ]
  %2:11 = splice.exec "chmod" at 2140(%0, %1#0, %1#1, %1#2, %1#3, %1#4, %1#5, %1#6, %1#7, %1#8, %1#9, %1#10) -> [
      #splice.entry<"/build/source" dir 755 mtime 1>,
      #splice.entry<"/build/source/.gitignore" file 644 mtime 1 "b6c49994d4ac8e9c963f86fbbcbdc602e09f4fcea204b4cc1e2467e271a0f1d4">,
      #splice.entry<"/build/source/LICENSE" file 644 mtime 1 "72c7d262e2c9ddf019edc90af852a01a410338efff4e75e1c005b77ada4be7c4">,
      #splice.entry<"/build/source/Makefile" file 644 mtime 1 "dc8a4358768ef0bdf20e064cd5203ac5c67fb5e283acd5e2fcb0fae9b360f517">,
      #splice.entry<"/build/source/README.ja.md" file 644 mtime 1 "2a01b6b7d7fb8e658a22b0c80633f8bf3da3bed5cb1a19c347bcf8c3e71a5adc">,
      #splice.entry<"/build/source/README.md" file 644 mtime 1 "2e602db77ec71fc2e9fa75f85c5d1a33588cb613227df2d6e8d02eff016dc7f4">,
      #splice.entry<"/build/source/demo.gif" file 644 mtime 1 "cd29dd2d45e941679a71762e1fdd315e040034cfd9c2849d2bf60ae3e7f2be6b">,
      #splice.entry<"/build/source/sl.1" file 644 mtime 1 "a598abda4950f6dfca0351cacf776267da065cfe67c540ec2a21ed4039a334f7">,
      #splice.entry<"/build/source/sl.1.ja" file 644 mtime 1 "e516ffe42b2b0993e5dbfcf216314342b10a2f5a2eacfa7d9d5fc6bfa2ba0581">,
      #splice.entry<"/build/source/sl.c" file 644 mtime 1 "bb61dbb2c20ba279cb96e2c21c0a5ad8f8a6bb0b74bce7b4268e8d1434138bb1">,
      #splice.entry<"/build/source/sl.h" file 644 mtime 1 "02847f3f0a00fb0f8d5ccb493ae0f00dc8617a8c416c9a4ecd1b5a0d0ee7c4d7">
  ]
  %3 = splice.exec "make" at 2762(%0, %2#0, %2#3, %2#9, %2#10) -> [
      #splice.entry<"/build/source/sl" file 755 mtime 1794960000 "f81e4c853bb247377aed69cc62781458530732bad945f45bfcfe60cfe12d7694">
  ]
  %4:3 = splice.exec "install" at 3345(%0, %2#0, %3) -> [
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/bin" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/bin/sl" file 755 mtime 1794960000 "f81e4c853bb247377aed69cc62781458530732bad945f45bfcfe60cfe12d7694">
  ]
  %5:5 = splice.exec "install" at 3372(%0, %2#0, %2#7, %2#8, %4#0) -> [
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share/man" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share/man/man1" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share/man/man1/sl.1" file 644 mtime 1794960000 "a598abda4950f6dfca0351cacf776267da065cfe67c540ec2a21ed4039a334f7">,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share/man/man1/sl.1.ja" file 644 mtime 1794960000 "e516ffe42b2b0993e5dbfcf216314342b10a2f5a2eacfa7d9d5fc6bfa2ba0581">
  ]
  %6 = splice.exec "find|patchelf" at 3607(%0, %2#0, %4#0, %4#1, %4#2) -> [
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/bin/sl" file 755 mtime 1794960000 "083fe23d6f7b5e542165516652a9fda8b722e71403e3d1c8b2c7ea0b1701b67c">
  ]
  %7:2 = splice.exec "find|xargs" at 3901(%0, %2#0, %4#0, %5#0, %5#1, %5#2, %5#3, %5#4) -> [
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share/man/man1/sl.1.gz" file 644 mtime 1794960000 "693d688135a74230c743b33d157d21be3d55409e49d7834e019dc2034cfd1872">,
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/share/man/man1/sl.1.ja.gz" file 644 mtime 1794960000 "696ec8692ec4b2306f29708c421a4e638b168bef2421d9cbbd8e41e38d8b18bc">
  ]
  %8 = splice.literal "mktemp" at 4090 demoted -> [
      #splice.entry<"/build/striperr.51ZNK0" file 600 mtime 1794960000 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">
  ]
  %9 = splice.exec "find|sort|cut|xargs" at 4121(%0, %2#0, %8, %4#0, %4#1, %6) -> [
      #splice.entry<"/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05/bin/sl" file 755 mtime 1794960000 "a6cd1b3e5b91dc867985bdb433c507ddd3a26e3be7974c24b967c968e281e0c7">
  ]
  splice.output "out" "/nix/store/0n7j2s7fawm2m7m3zmqapsjwv8xlq3yf-sl-5.05"(%4#0, %4#1, %9, %5#0, %5#1, %5#2, %7#0, %7#1)
}

