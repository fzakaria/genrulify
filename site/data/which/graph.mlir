module {
  %0:3 = splice.literal "sandbox" at 721 -> [
      #splice.entry<"/build" dir 700 mtime 1794960000>,
      #splice.entry<"/build/.attrs.json" file 644 mtime 1794960000 "4f4230357c86c3edac1952b7aeeba4ff276e2baaebced19a381d68f4d97ec87e">,
      #splice.entry<"/build/.attrs.sh" file 644 mtime 1794960000 "20900be40f21f4e7daa2d63b6f4f18364747457cd4d6d3c6b17d64b083f7f472">
  ]
  %1:72 = splice.exec "tar" at 1374(%0#0) -> [
      #splice.entry<"/build/which-2.25" dir 755 mtime 1779379815>,
      #splice.entry<"/build/which-2.25/AUTHORS" file 644 mtime 1738433876 "a97cd692ae5f31286bc897a42f68509ab89fa38bf65ca590f3a3d732b5e56d55">,
      #splice.entry<"/build/which-2.25/COPYING" file 644 mtime 1736555171 "8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903">,
      #splice.entry<"/build/which-2.25/EXAMPLES" file 644 mtime 1779379735 "024c2b01b644636c6b570f96da3dd06de2fc4525a383e8aeb339d2472ef18ce4">,
      #splice.entry<"/build/which-2.25/INSTALL" file 644 mtime 1779372694 "9abb72fe15eb716e3e5dd4703cd86f53caa2b72c0179d0ec91823f04297a48d9">,
      #splice.entry<"/build/which-2.25/Makefile.am" file 644 mtime 1738433876 "8c88e5a1eea25310810f9f2142413332db6b4405933d1017ca91d201d4d8373f">,
      #splice.entry<"/build/which-2.25/Makefile.in" file 644 mtime 1779379212 "f60ffb5614ddfc6fcbd5f9f437e34d17fd9d07a7e75f7518f16b52d23406428f">,
      #splice.entry<"/build/which-2.25/NEWS" file 644 mtime 1779378944 "90340270dfbf35bbeb01704f793845ff91d84195fc0084ff47f0bcd2b0871635">,
      #splice.entry<"/build/which-2.25/README" file 644 mtime 1779379558 "8f113e6e80ece2843f174ea2e82fc6b5ce1e82abcef3cb516f6dd5f945966113">,
      #splice.entry<"/build/which-2.25/README.alias" file 644 mtime 1744722861 "3df64cafbfb86641fbb8951805bd8ae1aa83e89c93fa441858de3845ce4127dd">,
      #splice.entry<"/build/which-2.25/README.md" file 644 mtime 1738433876 "a0cd2af43d06d6f2233bb2d68fc6d2db4965a63e0b6fda314e06de195be6172d">,
      #splice.entry<"/build/which-2.25/aclocal.m4" file 644 mtime 1779379212 "59fa3c998f3ae8a82e5b756c72df9cb8ccdd543e3d6fb370f034e299f06bcbc2">,
      #splice.entry<"/build/which-2.25/bash.c" file 644 mtime 1738433876 "927235f4867138e605643e47a0c1028032378da703636a1cc231be68b9dd6f79">,
      #splice.entry<"/build/which-2.25/bash.h" file 644 mtime 1738433876 "ec441d98ff81ad352864a550c4b8228cffb8d225926d02f750c168396ea325c7">,
      #splice.entry<"/build/which-2.25/compile" file 755 mtime 1752322329 "92ed7feeda61ad958dc3c4cc4efd724e1414482f1cad60b74b09e0633a93dfd6">,
      #splice.entry<"/build/which-2.25/config.guess" file 755 mtime 1752322329 "e3d148130e9151735f8b9a8e69a70d06890ece51468a9762eb7ac0feddddcc2f">,
      #splice.entry<"/build/which-2.25/config.h.in" file 644 mtime 1779379283 "24f2a2a52412f4a0abde888035013ea2ed650349f5aa949ac9b1ffeb5f93dc37">,
      #splice.entry<"/build/which-2.25/config.sub" file 755 mtime 1752322329 "11c54f55c3ac99e5d2c3dc2bb0bcccbf69f8223cc68f6b2438daa806cf0d16d8">,
      #splice.entry<"/build/which-2.25/configure" file 755 mtime 1779379212 "9f4eca40ae14a326ec58f0cb7c5cf7915709e610e206d6cc88ee83680ad34f43">,
      #splice.entry<"/build/which-2.25/configure.ac" file 644 mtime 1779378874 "91938a16562d4cc21033a7734fb80a9474112bb8445a55fd3ae267ef50428d2b">,
      #splice.entry<"/build/which-2.25/cwm4" dir 755 mtime 1779379815>,
      #splice.entry<"/build/which-2.25/cwm4/aclocal" dir 755 mtime 1779379815>,
      #splice.entry<"/build/which-2.25/cwm4/aclocal/AX_CHECK_COMPILE_FLAG.m4" file 644 mtime 1736629485 "c90340961d019589c519f12dfbc99ade152b6c798bcc30d8c02cd46be4d34abd">,
      #splice.entry<"/build/which-2.25/cwm4/aclocal/CW_LIB_LIBCWD.m4" file 644 mtime 1736629485 "de85753ee034edd7b59a0aae2fc9eaea9e9d9ce0a0b851dec46ee1b01878dd7c">,
      #splice.entry<"/build/which-2.25/cwm4/aclocal/CW_OPG_CXXFLAGS.m4" file 644 mtime 1736629485 "c3ed2580eabbb39144706520980179688911e3c9c93050367383073b8e2ea03c">,
      #splice.entry<"/build/which-2.25/cwm4/cmake" dir 755 mtime 1772052223>,
      #splice.entry<"/build/which-2.25/cwm4/cmake/AICxxProject" file 644 mtime 1768337404 "fff9125b39a405c45db4930fe92bb7317764e5f26d37e6aa41e841f27a83d8f2">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/AICxxProject.cmake" file 644 mtime 1736629485 "2d1e1557450018e1657ca3af2d975d83006cd655f48c831e2a1a3d71613dc6b4">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/AICxxSubmodules.cmake" file 644 mtime 1768337404 "61ef9291b5a05948affc6636cf9f25847338b6a95eb511b70343f02adb5dd8f4">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_CHECK_BMI2_SUPPORT.c" file 644 mtime 1736629485 "96965a7cfc987f5f5740858cf55f635da639efc19221f7bcdcf7fd63b43f6c16">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_CHECK_BMI2_SUPPORT.cmake" file 644 mtime 1768337404 "700a10ca7eae9262d3203facfbd191f5229c1689642bd9be91305dd94fcc6d9e">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_CHECK_REQUIRED_INCLUDE_FILE_CXX.cmake" file 644 mtime 1736629485 "85e1e24d78e05b1b0e20fa1f74a35b9fcbf6c4c42b3e5e2f336a09216ce3e7d9">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_DEFINE_TYPE.cmake" file 644 mtime 1736629485 "e49d7261d54e4c7f151bc618253eef4341f5546c460d4b5db6494e4effd253ed">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_OPTIONS.cmake" file 644 mtime 1772052223 "5b6dfff6e503e87c63fdc0dd9ef3baf27f0a458dc2267c2b9d04ac9a167921fb">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_SYS_CACHELINE_SIZE.c" file 644 mtime 1768337404 "7cce6938eba079c48b76a63b79da72e9e1598436119b44ff81db1c85aa7bbcd7">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_SYS_CACHELINE_SIZE.cmake" file 644 mtime 1736629485 "f8117b5a9f7b355ed75bb241a54736c4a593daad604d7e0b99cd9562f3fdf819">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_SYS_MALLOC_OVERHEAD.c" file 644 mtime 1736629485 "499ae85f61b262d714b1bdb426d21546554b552c9b7eb75358b0852fa5e2051e">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/CW_SYS_MALLOC_OVERHEAD.cmake" file 644 mtime 1736629485 "c8099df37fd3b33dbbebee5335567745135c5d56ea999317ffc603d22512350e">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/Findfarmhash.cmake" file 644 mtime 1736629485 "74322135b9dd104ded3b375e98bbcffc5db5c9621bbe47fa11ac33656dab3173">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/Findrt.cmake" file 644 mtime 1736629485 "a62da7d47f905615a1132dd7099e46477367aad053d83ef2db56e55b67d60247">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/Findwolfssl.cmake" file 644 mtime 1736629485 "d7d7fd5a321764332fbf29971473598013c9f8f63289e6baac9bc680d389aa80">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/StableGitache" file 644 mtime 1768337404 "e352b34df933cbd6e93996e7251b96ac84b9a5d2232733fe49f3b89fc9863633">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/ccache.cmake" file 644 mtime 1736629485 "4ac193d6566bf51b19892a8868a000ff51977891be406ec76c0b1f6c93e18890">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/color_vars.cmake" file 644 mtime 1736629485 "de5534ceba2b45ee593e8d98cb2e4de423debbb6dc7dfb6ac8a489c0eed32748">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/dump_cmake_variables.cmake" file 644 mtime 1736629485 "a143381adee875c1b811457f1d3756d59a3c5731b902052bb3246022facddf42">,
      #splice.entry<"/build/which-2.25/cwm4/cmake/print_target_properties.cmake" file 644 mtime 1736629485 "771e04881810ed8625b2b3a8b51560d0acb7fe07650a6197fa4f5a88b443b7dc">,
      #splice.entry<"/build/which-2.25/cwm4/configure_ac_bottom.m4" file 644 mtime 1736629485 "e993d306066a552d924f033634959570f6aa2368e98b174649b00f05629a400e">,
      #splice.entry<"/build/which-2.25/cwm4/configure_ac_top.m4" file 644 mtime 1772052341 "5473156afb256ce33a4e639ff26181693ad34ad7deaf9ccff69ddacadcf5cf17">,
      #splice.entry<"/build/which-2.25/cwm4/root_makefile_bottom.am" file 644 mtime 1736629485 "9611abc73941a78bf93704d847dd5450788b518fa5ec418bcbb909922f3b4d56">,
      #splice.entry<"/build/which-2.25/cwm4/root_makefile_top.am" file 644 mtime 1736629485 "2bcc92b0edea9a45715bb819a83aacf665313a055717db4a79fe44f1233910c0">,
      #splice.entry<"/build/which-2.25/depcomp" file 755 mtime 1752322329 "ac3ae010382a83144ff6ec1a4c368a3f16a14ea2b12751aa73ddfb548d931574">,
      #splice.entry<"/build/which-2.25/getopt.c" file 644 mtime 1738433876 "10f33debdb8b56af46ec8ec9560395f83c4c433a122400b64aeb880f6e330933">,
      #splice.entry<"/build/which-2.25/getopt.h" file 644 mtime 1738433876 "39ce163c8954c8fcacac7a9fad8328d8249c584ba9baaf8f0feff769afdd730e">,
      #splice.entry<"/build/which-2.25/getopt1.c" file 644 mtime 1738433876 "62f6f18fc4d50c0122487a996a143d4393a61bb68f1b72002fb93ae399db9f0a">,
      #splice.entry<"/build/which-2.25/install-sh" file 755 mtime 1752322329 "776876b3909b096439109a4c7642eb0f9100f6ba3fb42fa93d05351e50a0a7ef">,
      #splice.entry<"/build/which-2.25/m4" dir 755 mtime 1779379815>,
      #splice.entry<"/build/which-2.25/m4/BASH_CHECK_GETPW_FUNCS.m4" file 644 mtime 1738433876 "025defed5a0dc3766d0d864e25138df869658fcc480dd06c085a4d082ad80844">,
      #splice.entry<"/build/which-2.25/m4/min_automake_version.m4" file 644 mtime 1779379211 "fa207e9c8400b4111cecea82ad50e50fd85ed364de1ef577cb0e781ecd6503c3">,
      #splice.entry<"/build/which-2.25/m4/submodules.m4" file 644 mtime 1779379210 "192665d3d9679960805a46d7276ef6ac8570e48f955856b2608c5e448c30780d">,
      #splice.entry<"/build/which-2.25/maintMakefile.in" file 644 mtime 1779372694 "9785f11f0e8c0ba054f2b0b157083e43fb6552b6cf27609377e562b35d7626f3">,
      #splice.entry<"/build/which-2.25/mdate-sh" file 755 mtime 1736623918 "bcedc51c8cd766881cd159e2dfe2f85804f93d1efebce38c5b3a13269e3dc5be">,
      #splice.entry<"/build/which-2.25/missing" file 755 mtime 1752322329 "4f17c4a66ec5b8000d44573a576f6b5d9c0c84848e8fd1286f84a52f439db4e9">,
      #splice.entry<"/build/which-2.25/posixstat.h" file 644 mtime 1738433876 "317438350953469ea26a023355ae6e49bc1f68be01c698a77a61ac7d310a9438">,
      #splice.entry<"/build/which-2.25/stamp-vti" file 644 mtime 1779379283 "64029fa81ba31af4f449418a8a8abcc24090fe269800aa15987a9896b21357cd">,
      #splice.entry<"/build/which-2.25/sys.h" file 644 mtime 1738433876 "a72ea776121ee923233f3d9281a46c6a15ffa1da87d7a05dfbcb76bcce4c568e">,
      #splice.entry<"/build/which-2.25/tilde.c" file 644 mtime 1779372694 "eb244394676d0f25a3bbfee93d1ffaa280a033e3b70d84757db2fcf0e1075a49">,
      #splice.entry<"/build/which-2.25/tilde.h" file 644 mtime 1738433876 "5a97a0d2f76e3e70738049df030a417856a083ed9f794745f3b4a438a6c79baf">,
      #splice.entry<"/build/which-2.25/version.texi" file 644 mtime 1779379283 "64029fa81ba31af4f449418a8a8abcc24090fe269800aa15987a9896b21357cd">,
      #splice.entry<"/build/which-2.25/which.1" file 644 mtime 1779379283 "95edc68993685a096e3b353677c1092921a5a70d2008184a4f08c62ebfd39f86">,
      #splice.entry<"/build/which-2.25/which.c" file 644 mtime 1779372694 "59ce0c3f631b838a31f9466d28f525736a224a5f011b7d1bfe514e2920f7ee14">,
      #splice.entry<"/build/which-2.25/which.info" file 644 mtime 1779379283 "fbd0521595ab4553031fb69c81858bb358beeeef8241cd795c2ed41bc2a61065">,
      #splice.entry<"/build/which-2.25/which.texi" file 644 mtime 1744722930 "47e8a65580776983fd95fe6ccc458aadda1d6e643443d3dd7534d7d0cfba978c">
  ]
  %2 = splice.literal "glue" at 1830 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960000 "9e209ae34c96c356d7e0c394761321161a4080a67719202cf747f7fafe393b10">
  ]
  %3:2 = splice.exec "cat|patch" at 1871(%0#0, %1#0, %1#51, %1#52) -> [
      #splice.entry<"/build/which-2.25/getopt.c" file 644 mtime 1794960000 "a77271d9ffadc3aab76a71426c00f59b77598c79692c080b6ce4661431b916c4">,
      #splice.entry<"/build/which-2.25/getopt.h" file 644 mtime 1794960000 "4f8c6676cb0871f8893cd458b3c4701580ae28c73388574389ed55f3a9b775a5">
  ]
  %4 = splice.exec "cp" at 2011(%0#0, %1#0, %1#17) -> [
      #splice.entry<"/build/which-2.25/config.sub" file 755 mtime 1794960000 "fe3a2f32fbaff57848732549f48d983fd6526024ec2f0f5a9dc75c2f4359a3a6">
  ]
  %5 = splice.exec "cp" at 2055(%0#0, %1#0, %1#15) -> [
      #splice.entry<"/build/which-2.25/config.guess" file 755 mtime 1794960000 "641cae3c0c74c49354d3ede009f3febd80febe1501a77c1d9fac8d42cc45b6cb">
  ]
  %6 = splice.exec "find|basename|stat|mktemp|+4" at 2410(%0#0, %1#0, %1#18) -> [
      #splice.entry<"/build/which-2.25/configure" file 755 mtime 1779379212 "2b64f48d96cf63637d9cf39510d229370e7d4dba3a1206fae8d2d071f0836c0e">
  ]
  %7:5 = splice.exec "configure" at 2624(%0#0, %0#1, %0#2, %2, %1#0, %1#1, %1#2, %1#3, %1#4, %1#5, %1#6, %1#7, %1#8, %1#9, %1#10, %1#11, %1#12, %1#13, %1#14, %5, %1#16, %4, %6, %1#19, %1#20, %1#21, %1#22, %1#23, %1#24, %1#25, %1#26, %1#27, %1#28, %1#29, %1#30, %1#31, %1#32, %1#33, %1#34, %1#35, %1#36, %1#37, %1#38, %1#39, %1#40, %1#41, %1#42, %1#43, %1#44, %1#45, %1#46, %1#47, %1#48, %1#49, %1#50, %3#0, %3#1, %1#53, %1#54, %1#55, %1#56, %1#57, %1#58, %1#59, %1#60, %1#61, %1#62, %1#63, %1#64, %1#65, %1#66, %1#67, %1#68, %1#69, %1#70, %1#71) -> [
      #splice.entry<"/build/which-2.25/Makefile" file 644 mtime 1794960001 "a30fd1f15e962fc5a66aa72f382e5ec84ced09ccb9ce97dacbebeb3ed29789e4">,
      #splice.entry<"/build/which-2.25/config.h" file 644 mtime 1794960001 "ef606ae495edccdab6d4274458b779de7b8d04b4663feb341a58a8a1ade69fb8">,
      #splice.entry<"/build/which-2.25/config.status" file 755 mtime 1794960001 "3ca34bfcd98685d5c37c60b42cf9203b3fd09084d524061dd52aaab9ef909c31">,
      #splice.entry<"/build/which-2.25/maintMakefile" file 644 mtime 1794960001 "538bb2eac4b13453b2a9b25394ce0eb5b41e3ba070d16194c6f84e7abf7413de">,
      #splice.entry<"/build/which-2.25/stamp-h1" file 644 mtime 1794960001 "24fdde0aee1f49666fc7a5bd7f422dc0118e4db128bc1f606f7f047c9b245bb4">
  ]
  %8 = splice.literal "configure" at 2624 demoted -> [
      #splice.entry<"/build/which-2.25/config.log" file 644 mtime 1794960001 "6d0c36f41980a76e58fd58c64aca53d87352d4af8af840cca0b9ff1fae3254a1">
  ]
  %9 = splice.literal "glue" at 27777 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960001 "202bd8af1a533e4dd8db102d503d72ade9901345664423c83b3239ab1cab1027">
  ]
  %10:6 = splice.exec "make" at 27848(%0#0, %0#1, %0#2, %9, %1#0, %1#1, %1#2, %1#3, %1#4, %7#0, %1#5, %1#6, %1#7, %1#8, %1#9, %1#10, %1#11, %1#12, %1#13, %1#14, %5, %7#1, %1#16, %8, %7#2, %4, %6, %1#19, %1#20, %1#21, %1#22, %1#23, %1#24, %1#25, %1#26, %1#27, %1#28, %1#29, %1#30, %1#31, %1#32, %1#33, %1#34, %1#35, %1#36, %1#37, %1#38, %1#39, %1#40, %1#41, %1#42, %1#43, %1#44, %1#45, %1#46, %1#47, %1#48, %1#49, %1#50, %3#0, %3#1, %1#53, %1#54, %1#55, %1#56, %1#57, %1#58, %7#3, %1#59, %1#60, %1#61, %1#62, %7#4, %1#63, %1#64, %1#65, %1#66, %1#67, %1#68, %1#69, %1#70, %1#71) -> [
      #splice.entry<"/build/which-2.25/bash.o" file 644 mtime 1794960001 "b4e25ffe881021e9545fa39cdbcdab98a41879e10f28f727b1a5c4a55599ea81">,
      #splice.entry<"/build/which-2.25/getopt.o" file 644 mtime 1794960001 "5aface3cab2ab248daf206aaab6fe0457621a28af0d7093a1c2c11d8e956b81e">,
      #splice.entry<"/build/which-2.25/getopt1.o" file 644 mtime 1794960001 "6534ef016ccb918e1933d77657951e4ac1eb24ee7c739edf606cc629090791be">,
      #splice.entry<"/build/which-2.25/tilde.o" file 644 mtime 1794960002 "9af10279872a89ae40251fb7abc900cac0db9379479f0c7c7c9138b78c850a2d">,
      #splice.entry<"/build/which-2.25/which" file 755 mtime 1794960002 "be27c96a45d289df6aee0bbd9f80521cf644e81095cb0ce62ab0ec4396a431b8">,
      #splice.entry<"/build/which-2.25/which.o" file 644 mtime 1794960001 "a02f0483a31f07967e398a1eafb02fc3c34668a98f8fc8b39ba4c055094df5ba">
  ]
  %11 = splice.literal "glue" at 29049 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960002 "202bd8af1a533e4dd8db102d503d72ade9901345664423c83b3239ab1cab1027">
  ]
  %12 = splice.exec "mkdir" at 29076(%0#0, %1#0) -> [
      #splice.entry<"/nix/store/mdwxdimw26b72k7wign651n8clh4jzii-which-2.25" dir 755 mtime 1794960002>
  ]
  %13:11 = splice.exec "make" at 29142(%0#0, %0#1, %0#2, %11, %1#0, %1#1, %1#2, %1#3, %1#4, %7#0, %1#5, %1#6, %1#7, %1#8, %1#9, %1#10, %1#11, %1#12, %1#13, %10#0, %1#14, %5, %7#1, %1#16, %8, %7#2, %4, %6, %1#19, %1#20, %1#21, %1#22, %1#23, %1#24, %1#25, %1#26, %1#27, %1#28, %1#29, %1#30, %1#31, %1#32, %1#33, %1#34, %1#35, %1#36, %1#37, %1#38, %1#39, %1#40, %1#41, %1#42, %1#43, %1#44, %1#45, %1#46, %1#47, %1#48, %1#49, %1#50, %3#0, %3#1, %10#1, %1#53, %10#2, %1#54, %1#55, %1#56, %1#57, %1#58, %7#3, %1#59, %1#60, %1#61, %1#62, %7#4, %1#63, %1#64, %1#65, %1#66, %10#3, %1#67, %10#4, %1#68, %1#69, %1#70, %10#5, %1#71, %12) -> [
      #splice.entry<"/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man/share" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man/share/man" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man/share/man/man1" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man/share/man/man1/which.1" file 644 mtime 1794960002 "95edc68993685a096e3b353677c1092921a5a70d2008184a4f08c62ebfd39f86">,
      #splice.entry<"/nix/store/mdwxdimw26b72k7wign651n8clh4jzii-which-2.25/bin" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/mdwxdimw26b72k7wign651n8clh4jzii-which-2.25/bin/which" file 755 mtime 1794960002 "be27c96a45d289df6aee0bbd9f80521cf644e81095cb0ce62ab0ec4396a431b8">,
      #splice.entry<"/nix/store/yzrbsxy63iq4mclf51b57nlfjvdwwg5w-which-2.25-info" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/yzrbsxy63iq4mclf51b57nlfjvdwwg5w-which-2.25-info/share" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/yzrbsxy63iq4mclf51b57nlfjvdwwg5w-which-2.25-info/share/info" dir 755 mtime 1794960002>,
      #splice.entry<"/nix/store/yzrbsxy63iq4mclf51b57nlfjvdwwg5w-which-2.25-info/share/info/which.info" file 644 mtime 1794960002 "fbd0521595ab4553031fb69c81858bb358beeeef8241cd795c2ed41bc2a61065">
  ]
  %14 = splice.exec "find|xargs" at 30782(%0#0, %1#0, %13#0, %13#1, %13#2, %13#3, %13#4) -> [
      #splice.entry<"/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man/share/man/man1/which.1.gz" file 644 mtime 1794960002 "2f6aaf53f1302fa796163da6170c252fcae81871047f1862f00fe27fc2d129ee">
  ]
  %15 = splice.exec "find|patchelf" at 30956(%0#0, %1#0, %12, %13#5, %13#6) -> [
      #splice.entry<"/nix/store/mdwxdimw26b72k7wign651n8clh4jzii-which-2.25/bin/which" file 755 mtime 1794960002 "129a19318fa85cbf3267fcee76ad7ede37801450b1325e83088836deba82cf78">
  ]
  %16 = splice.literal "mktemp" at 31298 demoted -> [
      #splice.entry<"/build/striperr.mpDM8h" file 600 mtime 1794960002 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">
  ]
  %17 = splice.exec "find|sort|cut|xargs" at 31329(%0#0, %16, %1#0, %12, %13#5, %15) -> [
      #splice.entry<"/nix/store/mdwxdimw26b72k7wign651n8clh4jzii-which-2.25/bin/which" file 755 mtime 1794960002 "e650afe0d71ff2532bb83e6ef8cf0944f4a37f4ee92dff9ea40699bb998eb01b">
  ]
  splice.output "info" "/nix/store/yzrbsxy63iq4mclf51b57nlfjvdwwg5w-which-2.25-info"(%13#7, %13#8, %13#9, %13#10)
  splice.output "man" "/nix/store/2qqgq8ajvhai10hw5mids7djjs1m19z6-which-2.25-man"(%13#0, %13#1, %13#2, %13#3, %14)
  splice.output "out" "/nix/store/mdwxdimw26b72k7wign651n8clh4jzii-which-2.25"(%12, %13#5, %17)
}

