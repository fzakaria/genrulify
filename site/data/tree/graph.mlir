module {
  %0 = splice.literal "sandbox" at 716 -> [
      #splice.entry<"/build" dir 700 mtime 1>
  ]
  %1:26 = splice.exec "cp" at 2013(%0) -> [
      #splice.entry<"/build/source" dir 555 mtime 1>,
      #splice.entry<"/build/source/.gitignore" file 444 mtime 1 "b4e850f9061ee1509750fcff6da267913ebc0cf13c7fd15993b49f0a545d507b">,
      #splice.entry<"/build/source/CHANGES" file 444 mtime 1 "0df99815826ea934a2581edad9a843bd515fbea8e698ccfcbf843a6196c2486d">,
      #splice.entry<"/build/source/INSTALL" file 444 mtime 1 "e9a62db11f30e78bfaaf8a7c411f54efd55328fb3808d216484969be0ecd1472">,
      #splice.entry<"/build/source/LICENSE" file 444 mtime 1 "204d8eff92f95aac4df6c8122bc1505f468f3a901e5a4cc08940e0ede1938994">,
      #splice.entry<"/build/source/Makefile" file 444 mtime 1 "a10d14886cb9afec20a4092f5c4a1eff4a85b57f4430a6971ca18f006fd16b98">,
      #splice.entry<"/build/source/README" file 444 mtime 1 "b312af5c6e23161b1e2b57bf01d7988bef7c2d71e4eaca2b7e69fc9118df9051">,
      #splice.entry<"/build/source/TODO" file 444 mtime 1 "3da440d07a070d02ae151189a105df7f92196535d07ec2fa677b10a8f68b5b5b">,
      #splice.entry<"/build/source/color.c" file 444 mtime 1 "baa87ee27a9694933f3d8e7352b066805bfeb8e3bb82d673374b50701e24d98d">,
      #splice.entry<"/build/source/doc" dir 555 mtime 1>,
      #splice.entry<"/build/source/doc/global_info" file 444 mtime 1 "ad9796bff7b3f049d4b9f774cb01d5536c73201e1196381b8f719243b8f7cc0a">,
      #splice.entry<"/build/source/doc/tree.1" file 444 mtime 1 "01fc4e8a5f0f6dfad1a7b56dfa5e2d100b3b7a913ab7618dd7bfbad604138fc0">,
      #splice.entry<"/build/source/doc/xml.dtd" file 444 mtime 1 "a5a71b0b73ffad38c3d3a3f753b0da759b0eb7a011a4cc0ba0dc2028290aafef">,
      #splice.entry<"/build/source/file.c" file 444 mtime 1 "8bcb1bb5c6ff1231b280d70178c8cf6475dd5b307a5a90bae85214142e2fb070">,
      #splice.entry<"/build/source/filter.c" file 444 mtime 1 "0b063e4be02e0bc6275bb1dbc8dcd14cda0a9e1b067f46ae0f83f4ca4dd5b930">,
      #splice.entry<"/build/source/hash.c" file 444 mtime 1 "244c5a7d6f0088dc3eda4b10c9b7f4e25d5eab30d2f0ec66d195815bd6057c57">,
      #splice.entry<"/build/source/html.c" file 444 mtime 1 "4dc74098cba6248193b8d976d102f223cd16b0217c2d8949d86eef70331eafb2">,
      #splice.entry<"/build/source/info.c" file 444 mtime 1 "d9ba8789ffd2f738b447cd65c7ed9221e607f1a30110c375dd79e416addc65dd">,
      #splice.entry<"/build/source/json.c" file 444 mtime 1 "32a1927072bb20bd0fc1bdea3abc8d7180574a2358fa814707211e00a35fe7d5">,
      #splice.entry<"/build/source/list.c" file 444 mtime 1 "1c747c09c1197c8b33c9f4b740572b87c55ce588463dba682bc99890555edc23">,
      #splice.entry<"/build/source/strverscmp.c" file 444 mtime 1 "4f85e943f83b0edf4737a239a8dcdcefb81db6a285a5a4a43af8bdfd17def538">,
      #splice.entry<"/build/source/tree.c" file 444 mtime 1 "e0b3995e8e3d0bb0926b7516aae8298017d0aa17b830bd4344cd4a1a3ff4d9ed">,
      #splice.entry<"/build/source/tree.h" file 444 mtime 1 "481df65362d79d666435099cb4a7c17dd10fc9f2fd2a067298846599ed0a55df">,
      #splice.entry<"/build/source/unix.c" file 444 mtime 1 "3adbb9be9a35a329facbcb7e7632f4ec0bc5ec5232d32e826c43200159609fd9">,
      #splice.entry<"/build/source/util.c" file 444 mtime 1 "25d859310988ae9fe31f4bf98430c4f643082e1cb2d2047302c28bf982c8e146">,
      #splice.entry<"/build/source/xml.c" file 444 mtime 1 "d2c35737c29b8dd0821424539bbd9b196f9a7922818516a7bbbc2dbf7da1c27a">
  ]
  %2:26 = splice.exec "chmod" at 2094(%0, %1#0, %1#1, %1#2, %1#3, %1#4, %1#5, %1#6, %1#7, %1#8, %1#9, %1#10, %1#11, %1#12, %1#13, %1#14, %1#15, %1#16, %1#17, %1#18, %1#19, %1#20, %1#21, %1#22, %1#23, %1#24, %1#25) -> [
      #splice.entry<"/build/source" dir 755 mtime 1>,
      #splice.entry<"/build/source/.gitignore" file 644 mtime 1 "b4e850f9061ee1509750fcff6da267913ebc0cf13c7fd15993b49f0a545d507b">,
      #splice.entry<"/build/source/CHANGES" file 644 mtime 1 "0df99815826ea934a2581edad9a843bd515fbea8e698ccfcbf843a6196c2486d">,
      #splice.entry<"/build/source/INSTALL" file 644 mtime 1 "e9a62db11f30e78bfaaf8a7c411f54efd55328fb3808d216484969be0ecd1472">,
      #splice.entry<"/build/source/LICENSE" file 644 mtime 1 "204d8eff92f95aac4df6c8122bc1505f468f3a901e5a4cc08940e0ede1938994">,
      #splice.entry<"/build/source/Makefile" file 644 mtime 1 "a10d14886cb9afec20a4092f5c4a1eff4a85b57f4430a6971ca18f006fd16b98">,
      #splice.entry<"/build/source/README" file 644 mtime 1 "b312af5c6e23161b1e2b57bf01d7988bef7c2d71e4eaca2b7e69fc9118df9051">,
      #splice.entry<"/build/source/TODO" file 644 mtime 1 "3da440d07a070d02ae151189a105df7f92196535d07ec2fa677b10a8f68b5b5b">,
      #splice.entry<"/build/source/color.c" file 644 mtime 1 "baa87ee27a9694933f3d8e7352b066805bfeb8e3bb82d673374b50701e24d98d">,
      #splice.entry<"/build/source/doc" dir 755 mtime 1>,
      #splice.entry<"/build/source/doc/global_info" file 644 mtime 1 "ad9796bff7b3f049d4b9f774cb01d5536c73201e1196381b8f719243b8f7cc0a">,
      #splice.entry<"/build/source/doc/tree.1" file 644 mtime 1 "01fc4e8a5f0f6dfad1a7b56dfa5e2d100b3b7a913ab7618dd7bfbad604138fc0">,
      #splice.entry<"/build/source/doc/xml.dtd" file 644 mtime 1 "a5a71b0b73ffad38c3d3a3f753b0da759b0eb7a011a4cc0ba0dc2028290aafef">,
      #splice.entry<"/build/source/file.c" file 644 mtime 1 "8bcb1bb5c6ff1231b280d70178c8cf6475dd5b307a5a90bae85214142e2fb070">,
      #splice.entry<"/build/source/filter.c" file 644 mtime 1 "0b063e4be02e0bc6275bb1dbc8dcd14cda0a9e1b067f46ae0f83f4ca4dd5b930">,
      #splice.entry<"/build/source/hash.c" file 644 mtime 1 "244c5a7d6f0088dc3eda4b10c9b7f4e25d5eab30d2f0ec66d195815bd6057c57">,
      #splice.entry<"/build/source/html.c" file 644 mtime 1 "4dc74098cba6248193b8d976d102f223cd16b0217c2d8949d86eef70331eafb2">,
      #splice.entry<"/build/source/info.c" file 644 mtime 1 "d9ba8789ffd2f738b447cd65c7ed9221e607f1a30110c375dd79e416addc65dd">,
      #splice.entry<"/build/source/json.c" file 644 mtime 1 "32a1927072bb20bd0fc1bdea3abc8d7180574a2358fa814707211e00a35fe7d5">,
      #splice.entry<"/build/source/list.c" file 644 mtime 1 "1c747c09c1197c8b33c9f4b740572b87c55ce588463dba682bc99890555edc23">,
      #splice.entry<"/build/source/strverscmp.c" file 644 mtime 1 "4f85e943f83b0edf4737a239a8dcdcefb81db6a285a5a4a43af8bdfd17def538">,
      #splice.entry<"/build/source/tree.c" file 644 mtime 1 "e0b3995e8e3d0bb0926b7516aae8298017d0aa17b830bd4344cd4a1a3ff4d9ed">,
      #splice.entry<"/build/source/tree.h" file 644 mtime 1 "481df65362d79d666435099cb4a7c17dd10fc9f2fd2a067298846599ed0a55df">,
      #splice.entry<"/build/source/unix.c" file 644 mtime 1 "3adbb9be9a35a329facbcb7e7632f4ec0bc5ec5232d32e826c43200159609fd9">,
      #splice.entry<"/build/source/util.c" file 644 mtime 1 "25d859310988ae9fe31f4bf98430c4f643082e1cb2d2047302c28bf982c8e146">,
      #splice.entry<"/build/source/xml.c" file 644 mtime 1 "d2c35737c29b8dd0821424539bbd9b196f9a7922818516a7bbbc2dbf7da1c27a">
  ]
  %3:14 = splice.exec "make" at 2769(%0, %2#0, %2#5, %2#8, %2#13, %2#14, %2#15, %2#16, %2#17, %2#18, %2#19, %2#20, %2#21, %2#22, %2#23, %2#24, %2#25) -> [
      #splice.entry<"/build/source/color.o" file 644 mtime 1794960000 "d9dca5a9c2a0fa378a4f2680cd2cba11cf41021516d15596fcecc9df2935e1f8">,
      #splice.entry<"/build/source/file.o" file 644 mtime 1794960000 "24147901c82f7b56fcc2185a542133fb432f2b8d4a30fa24b5db3503ed5ffbf3">,
      #splice.entry<"/build/source/filter.o" file 644 mtime 1794960000 "1d1187a525395fc204ae2916fa6ba0d7be6a29b257cc0488ebcde370f68424e3">,
      #splice.entry<"/build/source/hash.o" file 644 mtime 1794960000 "6c9c2f81180b4733867c3b1a4dfdb0306eba1e748c4d9edd3ba230c91e39e85b">,
      #splice.entry<"/build/source/html.o" file 644 mtime 1794960000 "d58dab50b72f610322cc775f81b752b639b30dd98eb4c328ffa20e254afffccd">,
      #splice.entry<"/build/source/info.o" file 644 mtime 1794960000 "d15d6dc6aa382f496450494e61499ba14539fb4082bde273931c0ff82929cd34">,
      #splice.entry<"/build/source/json.o" file 644 mtime 1794960000 "8549a299c66c2ef6cefc632115d4a27ca10e20ae246f5eccadb8810d69df15e8">,
      #splice.entry<"/build/source/list.o" file 644 mtime 1794960000 "ae20a2809febf1a45e3c26053e99a3409ec571e5bbb17bbcded16f3a3281bc92">,
      #splice.entry<"/build/source/strverscmp.o" file 644 mtime 1794960001 "5af672e531bcff1ab82b7c5cc287cd80cac5837e93583c114041c489d60a0160">,
      #splice.entry<"/build/source/tree" file 755 mtime 1794960001 "8b86ca37954b5340da2d2df07c4ee433dfad6f915c16e1f7e08833e61f465768">,
      #splice.entry<"/build/source/tree.o" file 644 mtime 1794960001 "d101370c02ca714abc423f7329104188300286240b9962835fe55a3b9133554a">,
      #splice.entry<"/build/source/unix.o" file 644 mtime 1794960001 "e0decf9c0415065e795b2ac3b49f6170023424942f75ec01ad848cd42713dd83">,
      #splice.entry<"/build/source/util.o" file 644 mtime 1794960001 "3767ea41cd728739291e3b72ccf7ebc8216a57f9589ec74e6754d63a3e766dbd">,
      #splice.entry<"/build/source/xml.o" file 644 mtime 1794960001 "ce336848601ec88ba585419e394999ff792287a4e8aee6b632fafd2a3ad2f91e">
  ]
  %4 = splice.literal "glue" at 4846 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960001 "c1da3840ba9034f5ab16d5e72d33c62727544157934f3d5a308c2f78fafce273">
  ]
  %5 = splice.exec "mkdir" at 4875(%0, %2#0) -> [
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2" dir 755 mtime 1794960001>
  ]
  %6:5 = splice.exec "make" at 4937(%0, %4, %2#0, %2#1, %2#2, %2#3, %2#4, %2#5, %2#6, %2#7, %2#8, %3#0, %2#9, %2#10, %2#11, %2#12, %2#13, %3#1, %2#14, %3#2, %2#15, %3#3, %2#16, %3#4, %2#17, %3#5, %2#18, %3#6, %2#19, %3#7, %2#20, %3#8, %3#9, %2#21, %2#22, %3#10, %2#23, %3#11, %2#24, %3#12, %2#25, %3#13, %5) -> [
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/bin" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/bin/tree" file 755 mtime 1794960001 "8b86ca37954b5340da2d2df07c4ee433dfad6f915c16e1f7e08833e61f465768">,
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/man" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/man/man1" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/man/man1/tree.1" file 644 mtime 1794960001 "01fc4e8a5f0f6dfad1a7b56dfa5e2d100b3b7a913ab7618dd7bfbad604138fc0">
  ]
  %7 = splice.exec "mkdir" at 5196(%0, %2#0, %5) -> [
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/share" dir 755 mtime 1794960001>
  ]
  %8:3 = splice.exec "mv" at 5217(%0, %2#0, %5, %6#2, %6#3, %6#4, %7) -> [
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/share/man" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/share/man/man1" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/share/man/man1/tree.1" file 644 mtime 1794960001 "01fc4e8a5f0f6dfad1a7b56dfa5e2d100b3b7a913ab7618dd7bfbad604138fc0">
  ]
  %9:2 = splice.exec "mkdir" at 5311(%0, %2#0) -> [
      #splice.entry<"/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man/share" dir 755 mtime 1794960001>
  ]
  %10:3 = splice.exec "mv" at 5332(%0, %2#0, %5, %7, %8#0, %8#1, %8#2, %9#0, %9#1) -> [
      #splice.entry<"/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man/share/man" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man/share/man/man1" dir 755 mtime 1794960001>,
      #splice.entry<"/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man/share/man/man1/tree.1" file 644 mtime 1794960001 "01fc4e8a5f0f6dfad1a7b56dfa5e2d100b3b7a913ab7618dd7bfbad604138fc0">
  ]
  %11 = splice.exec "find|patchelf" at 5525(%0, %2#0, %5, %6#0, %6#1) -> [
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/bin/tree" file 755 mtime 1794960001 "46a159db6627ec69def35248eb761a755f1d9639fff28b41585e54812a1ed01a">
  ]
  %12 = splice.literal "mktemp" at 5865 demoted -> [
      #splice.entry<"/build/striperr.R4cSn6" file 600 mtime 1794960001 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">
  ]
  %13 = splice.exec "find|sort|cut|xargs" at 5897(%0, %2#0, %12, %5, %6#0, %11) -> [
      #splice.entry<"/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2/bin/tree" file 755 mtime 1794960001 "a043fb27f166aa44120d8a4f6e23707a0782bc29e554169b9c265f66a69dd0e3">
  ]
  %14 = splice.exec "find|xargs" at 6252(%0, %2#0, %9#0, %9#1, %10#0, %10#1, %10#2) -> [
      #splice.entry<"/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man/share/man/man1/tree.1.gz" file 644 mtime 1794960001 "d4f7f889b7d3cd784a701dd3d144200cb65f88942c1e5d4c264d629c41485055">
  ]
  splice.output "man" "/nix/store/symf4p80isdvxjvj4m2y7gffaqyhm9fs-tree-2.3.2-man"(%9#0, %9#1, %10#0, %10#1, %14)
  splice.output "out" "/nix/store/ggxc3i3q4k4f2czrv01adcnn3hhajak5-tree-2.3.2"(%5, %6#0, %13)
}

