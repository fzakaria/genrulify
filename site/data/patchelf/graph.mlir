module {
  %0:3 = splice.literal "sandbox" at 721 -> [
      #splice.entry<"/build" dir 700 mtime 1794960000>,
      #splice.entry<"/build/.attrs.json" file 644 mtime 1794960000 "b8bfe695cf7ef1ec1c17eb63a9702db7bf7598c095666754c7851876d118cc9c">,
      #splice.entry<"/build/.attrs.sh" file 644 mtime 1794960000 "ac0cecd5c99ece43e45c61f6eeb35b126151591a50caac62b45e3b2e157e16d1">
  ]
  %1:104 = splice.exec "tar" at 1274(%0#0) -> [
      #splice.entry<"/build/patchelf-0.15.2" dir 755 mtime 1755033524>,
      #splice.entry<"/build/patchelf-0.15.2/COPYING" file 644 mtime 1 "e79e9c8a0c85d735ff98185918ec94ed7d175efc377012787aebcf3b80f0d90b">,
      #splice.entry<"/build/patchelf-0.15.2/Makefile.am" file 644 mtime 1 "fc180971c3e6665262a4dade001e94128b2be082dd023f1267db850b060641ee">,
      #splice.entry<"/build/patchelf-0.15.2/Makefile.in" file 644 mtime 1755033521 "8181efa91a889541469b3939b9f08fd15438e1a9123ecc387a6676adc0a480a2">,
      #splice.entry<"/build/patchelf-0.15.2/README.md" file 644 mtime 1 "1914001a74394a6d34a61389de8fa6a1731e6a44a6609a66823467e3a7931091">,
      #splice.entry<"/build/patchelf-0.15.2/aclocal.m4" file 644 mtime 1755033521 "2384382da88d5ecde0732892576b3c7e56becdbc35c4860eb9620e2ad06680f1">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux" dir 755 mtime 1755033524>,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/compile" file 555 mtime 1755033521 "c207b390aac6323062b982214a6c63448e53e6911107993abe96f35fe7a30a18">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/depcomp" file 555 mtime 1755033521 "e44b49f71b265788187993090027193a6cd2b4718f9aa7be34412f537bce6873">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/install-sh" file 744 mtime 1755033522 "9953cae3336e4effe0ffb962a52b0b483ca73de77ad94375279a927ba18f6a86">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/missing" file 555 mtime 1755033521 "a9865db4f39574ff128c0312c367f070d20f81847817021ecce95fd70a610c9d">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/test-driver" file 555 mtime 1755033521 "08ff8986c3a0ce8bf32d25ec7e4752ffbce9c89589f018836c97a08b6573ac2c">,
      #splice.entry<"/build/patchelf-0.15.2/configure" file 755 mtime 1755033523 "baf795057085abebf2ba396a95bceb590ab7736f85aa4eb0e7c2793745b9d011">,
      #splice.entry<"/build/patchelf-0.15.2/configure.ac" file 644 mtime 1 "f7cc9aa0f47dac18f898a0258568e85c4c01f6a849fb0618681d17541387245a">,
      #splice.entry<"/build/patchelf-0.15.2/m4" dir 755 mtime 1755033524>,
      #splice.entry<"/build/patchelf-0.15.2/m4/ax_cxx_compile_stdcxx.m4" file 644 mtime 1 "9df083f4aaeb7dc3f4b45066f613960325e756abe4c6cc1f156fe76d3cff2159">,
      #splice.entry<"/build/patchelf-0.15.2/patchelf.1" file 644 mtime 1 "461346ec3323b56674132f919d832d98fbe57f7b10a09596c61d531cab21d013">,
      #splice.entry<"/build/patchelf-0.15.2/patchelf.spec" file 644 mtime 1755033524 "5550d9d1a83ef9a7c457499226b359f3b15bda64061d6d6ac69a74fb38ec9bcd">,
      #splice.entry<"/build/patchelf-0.15.2/patchelf.spec.in" file 644 mtime 1 "9e139bde79055cd260b85ad1aad094a2756d9f16d7694cbb1e367f92358b1cb2">,
      #splice.entry<"/build/patchelf-0.15.2/src" dir 755 mtime 1755033524>,
      #splice.entry<"/build/patchelf-0.15.2/src/Makefile.am" file 644 mtime 1 "e5ed0949e3846a4f649f1c099d66795acfe1213caad5845479ca2c1347ea1721">,
      #splice.entry<"/build/patchelf-0.15.2/src/Makefile.in" file 644 mtime 1755033521 "c9a90c936e6646d897a26f5fd728b128f7f0774880330905c0865830291e7d4e">,
      #splice.entry<"/build/patchelf-0.15.2/src/elf.h" file 644 mtime 1 "4d42687b690e3d18c76a3d23c6b472cd1786203f81f8f2299ffdf40327a551f2">,
      #splice.entry<"/build/patchelf-0.15.2/src/patchelf.cc" file 644 mtime 1 "2fb2d20cbec69e696065cf4018db894456c6db675bcd84c2fa87fec2813e9748">,
      #splice.entry<"/build/patchelf-0.15.2/src/patchelf.h" file 644 mtime 1 "8fb4f5fadfc74dd7c7ddafd6bf6600685a6c4227a71630827fe08e31f74fcefd">,
      #splice.entry<"/build/patchelf-0.15.2/tests" dir 755 mtime 1755033524>,
      #splice.entry<"/build/patchelf-0.15.2/tests/Makefile.am" file 644 mtime 1 "7178bd04a95733aa9eb3677631765e5e6d242f4cad438f0a4edcbe4bac363385">,
      #splice.entry<"/build/patchelf-0.15.2/tests/Makefile.in" file 644 mtime 1755033522 "d71d9b91eb4dbf4559f25be961162f534cbc8a53cfe31ba8cb9fb40e8c61d68c">,
      #splice.entry<"/build/patchelf-0.15.2/tests/add-debug-tag.sh" file 755 mtime 1 "b06d080902ba4df5a123614d524c49f6249738a8efdd04509129687556aa1af1">,
      #splice.entry<"/build/patchelf-0.15.2/tests/add-rpath.sh" file 755 mtime 1 "738478f6dc820511f0622de401e9c7642121d79dcbc1f1e5cec78df489690319">,
      #splice.entry<"/build/patchelf-0.15.2/tests/args-from-file.sh" file 755 mtime 1 "29174aa9ed196d1e6b59ccfd0232a6239767b5ee9f02311278ac91d80411b878">,
      #splice.entry<"/build/patchelf-0.15.2/tests/bar.c" file 644 mtime 1 "749017781fb0784af63ad7070cbf82e75292309b973221b6aeff535780cfb8b9">,
      #splice.entry<"/build/patchelf-0.15.2/tests/basic-flags.sh" file 755 mtime 1 "76d498ca34637bb420b9174e2790b974e3ff8da8b3390fd68e6c5a376e0797f3">,
      #splice.entry<"/build/patchelf-0.15.2/tests/big-dynstr.sh" file 755 mtime 1 "f8831fc7b016ef12602db687c156457a7b58c43eebb16605f06dab9086b28d25">,
      #splice.entry<"/build/patchelf-0.15.2/tests/build-id.sh" file 755 mtime 1 "55237161886b522e5090085eebe624b79201b36a2534433bb9b9ee187c603a26">,
      #splice.entry<"/build/patchelf-0.15.2/tests/contiguous-note-sections.ld" file 644 mtime 1 "130eb74746f96940e75717de7ae7cc3e525c2d7c3b97ffa2acc622f73bc56cdb">,
      #splice.entry<"/build/patchelf-0.15.2/tests/contiguous-note-sections.s" file 644 mtime 1 "d0c2daad489195ffe33de2aaba9f8f2b218ddf18cedba4fd1075bff7cae0a6cc">,
      #splice.entry<"/build/patchelf-0.15.2/tests/contiguous-note-sections.sh" file 755 mtime 1 "bd615427eb6c5392aa142f1a7a255d4788dea76285cf8c3c8ba69f4c767b79f5">,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness" dir 755 mtime 1>,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness.sh" file 755 mtime 1 "58882d709faec7891ae21001b3b65647fb2a3b90ffb3e0679dd411b267dd5cb5">,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/Makefile" file 644 mtime 1 "5ce845771e5641e80b559c1a5bc0aca62e692d7f54f00c43f89735a5e558fa7c">,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/ppc64" dir 755 mtime 1>,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/ppc64/libtest.so" file 755 mtime 1 "20063b172b0f2aec3c02a49e5d9e8b0e65ad28524f74b33ce0d423dc0b64b452">,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/ppc64/main" file 755 mtime 1 "306b413d09dbdc99e2b86e1d6b0f0b0e5492a3a762a2b90739cc1b77c66eef74">,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/ppc64le" dir 755 mtime 1>,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/ppc64le/libtest.so" file 755 mtime 1 "baec9a30e5b4d02c8f98b3146c935b22d243e25c8c0f31057bc820e61e75ce10">,
      #splice.entry<"/build/patchelf-0.15.2/tests/endianness/ppc64le/main" file 755 mtime 1 "a1b657122a48cb74319f64fe0acc2f45fba5cd6d5de39ac0e71fa8a168007668">,
      #splice.entry<"/build/patchelf-0.15.2/tests/foo.c" file 644 mtime 1 "407fd33c2c3345543f2c397b6feb74b640b483679051ba5b1459bf603e784c1a">,
      #splice.entry<"/build/patchelf-0.15.2/tests/force-rpath.sh" file 755 mtime 1 "f5bf1de4906d6935377a9b59ea1e542cc24988d852cdda8b79057d624ba2d3d1">,
      #splice.entry<"/build/patchelf-0.15.2/tests/grow-file.sh" file 755 mtime 1 "feaab0374b0b83e08d56c1efeb476031420332aa8b1ab1984814b5743b37f725">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf" dir 755 mtime 1>,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf.sh" file 755 mtime 1 "62f412f2251ec68cbb328705fcc1b5c9379d1da00efc57f4ee4ebdbf9d225c3f">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf/invalid-phdr-offset" file 644 mtime 1 "ad6347d2d394716f5e104044000c3a1f094dbfaad25ffd9ab56436f95b25e91a">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf/invalid-shdr-name" file 644 mtime 1 "61fe2bccbc06c0dc39b27a5701788083a28e18e358a6d65c22e43500b2e2f6fe">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf/invalid-shrstrtab-idx" file 644 mtime 1 "5869aa997091bb7c915f7d1c2f15eb233deee20e79ab0147a80cba24d60636a5">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf/invalid-shrstrtab-nonterm" file 644 mtime 1 "dda517b71cb735af8aa1226e1a2939387c5d13c3774d2357f4c266c9c7c378f8">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf/invalid-shrstrtab-size" file 644 mtime 1 "8858bdde2d1425b71ce476c88bed29e93a2f701c6f4117a0200d9f67dded3353">,
      #splice.entry<"/build/patchelf-0.15.2/tests/invalid-elf/invalid-shrstrtab-zero" file 644 mtime 1 "9a8b380f5b0761c8532a7e503516395410d99aeacf2d209728994266d46c4ac3">,
      #splice.entry<"/build/patchelf-0.15.2/tests/main.c" file 644 mtime 1 "b231de5a43d795cb0522aa134ef4ef80527a74c416c9dc3603286328bf83f596">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-dynamic-section.sh" file 755 mtime 1 "7af51befdfdc964ea19a3abbdb07e5c61cb4058a14db536c49b2baefd4dcf530">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-gnu-hash.sh" file 755 mtime 1 "05d85f32055290c662ef8245c4f8b1b937f37982a93a0dda859ed051a8c11d6a">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-pie-powerpc.sh" file 755 mtime 1 "d585e744dc34448e395076fef120ae96ff9be19368462d719ca48d5263dccbfb">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild" dir 755 mtime 1>,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild.sh" file 755 mtime 1 "76d4f72dea773ec767deff82d8c9c069b8ec4927aff451113ab8413e2634c8b3">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-amd64" file 755 mtime 1 "3ed3dff4c954840aa31df3d4432c49ca29504ac8bffc9f0e47f9f9f826ce8b07">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-armel" file 755 mtime 1 "669c151e81f616a16c005a3dcd23b3ee197e6c629a5330c0fdcf182c32c49222">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-armhf" file 755 mtime 1 "9b0d968d8817de7f6cee8c541483edd42ff07ab58ee74b54a388a656a4c2d51c">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-hurd-i386" file 755 mtime 1 "e96d567aaf766f7becef444d153a385d2d45f94bd74782adc7e5dc7ca040d4c6">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-i386" file 755 mtime 1 "abdb6ce248db3287480d717f258fa270b4d3c4bf46754e0b2fa9a69c4f760f70">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-ia64" file 755 mtime 1 "3f2d11dda2cfb246a219cb03888c7f500d54875c154dfa90be4141696f303fae">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-kfreebsd-amd64" file 755 mtime 1 "a31bb55d7c5265599204d54e32e6dc5e25e37352808436be98f267e56db6b36b">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-kfreebsd-i386" file 755 mtime 1 "0d05153dd805d166a50f8c0dead093a8568de4f22711c049b1d614c4ec0215a5">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-mips" file 755 mtime 1 "9daee623022c6b9d5325d3bd755653a8933af3443d5695f34063b88e4922d659">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-mipsel" file 755 mtime 1 "312a8b877d4e37b82f0fb412836053ef71d12ab7fe3fdc788846815b903707c1">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-pie-powerpc" file 755 mtime 1 "568f2ca54f06695973ec6114ba29a05e753eb28a663d5c3b5cdc344d3a27c303">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-powerpc" file 755 mtime 1 "c4ae586ca7affad8a6d665c6d9f12964f0e72276955ba76759193e3a8531d5a2">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-s390" file 755 mtime 1 "352dd41a6a7460095402b2b2d9e3bae7ab8dba6619b37d3c218c50440572889c">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-sh4" file 755 mtime 1 "f71f93b093777454e61f10c9d09a201288cb44ca69a1c2084281f7ab210ab470">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath-prebuild/no-rpath-sparc" file 755 mtime 1 "0e7fac1ae9b554df264028de3c7673482c8b38310c17066fdfb1b48d446ccfb7">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath.c" file 644 mtime 1 "1c6a302dcec1786e8e5690c34de02f089d50362bc67f6b3d361ded4113d5ded1">,
      #splice.entry<"/build/patchelf-0.15.2/tests/no-rpath.sh" file 755 mtime 1 "6adeeb9bc1fe425947c92fa86e13ce4fcc687e05b8006d2cb6fc49c152a75c15">,
      #splice.entry<"/build/patchelf-0.15.2/tests/output-flag.sh" file 755 mtime 1 "3f4644cb37f631c721e32fb2c816c8c83ef5ee1ac0d94f4bc24470929ee3604c">,
      #splice.entry<"/build/patchelf-0.15.2/tests/phdr-corruption.ld" file 644 mtime 1 "2983efad4afc08f1658fd2cd828558782bb29bede45bd008746c55b692d756a2">,
      #splice.entry<"/build/patchelf-0.15.2/tests/phdr-corruption.sh" file 755 mtime 1 "97ab32d412abea28883ab3fe46335f355c0001eaedf863e57be26af0d8985429">,
      #splice.entry<"/build/patchelf-0.15.2/tests/plain-fail.sh" file 755 mtime 1 "755d111687ac5de1c3a2124eb3d597a2dc202f546c21b19e1f58c9108cf75ed4">,
      #splice.entry<"/build/patchelf-0.15.2/tests/plain-needed.sh" file 755 mtime 1 "1fe95163762dd25d7bcc531a5fdb410e896570bd284b1d08b4d64b0c4e5f14dd">,
      #splice.entry<"/build/patchelf-0.15.2/tests/plain-run.sh" file 755 mtime 1 "3196ef6546f4ab6bf55fa5335a2b28a5bf82cf537f5843a8958ae37f86d764c9">,
      #splice.entry<"/build/patchelf-0.15.2/tests/replace-add-needed.sh" file 755 mtime 1 "5be4f65ccd6b936714d05b1b7037967b7b6beeb63605baa61dea754120e84ea7">,
      #splice.entry<"/build/patchelf-0.15.2/tests/replace-needed.sh" file 755 mtime 1 "4ca317c77d33fbb7a61ca4d34bd126b372810a838d3a60c9688dac9597cff8e9">,
      #splice.entry<"/build/patchelf-0.15.2/tests/set-empty-rpath.sh" file 755 mtime 1 "c004a807165068e29bcdd8b79fa9291f85a12f0dee9c6a99d71b6d75884c4a0d">,
      #splice.entry<"/build/patchelf-0.15.2/tests/set-interpreter-long.sh" file 755 mtime 1 "80177586bb514263220d24c53daccc02cdc9b5ef05ab392b24c45faebaba4b31">,
      #splice.entry<"/build/patchelf-0.15.2/tests/set-interpreter-short.sh" file 755 mtime 1 "712c98d70368cbbe4198302d2130d7612a4857802f432a4e282240c10e800ae8">,
      #splice.entry<"/build/patchelf-0.15.2/tests/set-rpath-library.sh" file 755 mtime 1 "da1705b42d06685ba5ca894f63aeab80884d0bc0cf4de05d2a647dd00253df17">,
      #splice.entry<"/build/patchelf-0.15.2/tests/set-rpath-rel-map.sh" file 755 mtime 1 "c646d475bb720d1243dfdacdf7047f95d304236d4b28dcb896b47b318d7b8cb4">,
      #splice.entry<"/build/patchelf-0.15.2/tests/set-rpath.sh" file 755 mtime 1 "e7dd3c095066b68afb43e13fe691953ee84c743886ef94cff4ff2a8ea064e5cc">,
      #splice.entry<"/build/patchelf-0.15.2/tests/shrink-rpath-with-allowed-prefixes.sh" file 755 mtime 1 "b2bfa773646ae05257d820c2d7d0ad3234fa216327d553634ff009660b07baee">,
      #splice.entry<"/build/patchelf-0.15.2/tests/shrink-rpath.sh" file 755 mtime 1 "8463ff63805adcdf878476a0b3ff94b870f5086ad8a9c89a7908459dec0d0e22">,
      #splice.entry<"/build/patchelf-0.15.2/tests/simple.c" file 644 mtime 1 "b3cb46948bbf0532801f26da5375472354523fc6019bcfc25a1154039641bf35">,
      #splice.entry<"/build/patchelf-0.15.2/tests/soname.sh" file 755 mtime 1 "b89e93522a4d885bb8bbba4ad97ec6f1c11f10cc8aa2b405640dde9c14019274">,
      #splice.entry<"/build/patchelf-0.15.2/tests/too-many-strtab.c" file 644 mtime 1 "adba6c5540c760e438379ce4802ee01cd843928ae6b7421211167b7d53cb7c6e">,
      #splice.entry<"/build/patchelf-0.15.2/tests/too-many-strtab.sh" file 755 mtime 1 "37c15537e27b740d4686e1a6ec54f4b9f5b15381f37151773770747a7aa75e14">,
      #splice.entry<"/build/patchelf-0.15.2/tests/too-many-strtab2.s" file 644 mtime 1 "40b2cb4e48e05c3bbb267b7a6b2d2b1175070b97ddee46e21c8fca97705e960e">,
      #splice.entry<"/build/patchelf-0.15.2/tests/void.c" file 644 mtime 1 "2df0720899262df4daf8898f2998fb8e54955f18c880bfe803adeeb6e2d2e4fd">,
      #splice.entry<"/build/patchelf-0.15.2/version" file 644 mtime 1755033519 "355a2e14f6a7f2cb387d9d3b75a3550980d020ee1d005b139b62290c1645068a">
  ]
  %2:4 = splice.exec "chmod" at 1573(%0#0, %1#0, %1#6, %1#7, %1#8, %1#10, %1#11) -> [
      #splice.entry<"/build/patchelf-0.15.2/build-aux/compile" file 755 mtime 1755033521 "c207b390aac6323062b982214a6c63448e53e6911107993abe96f35fe7a30a18">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/depcomp" file 755 mtime 1755033521 "e44b49f71b265788187993090027193a6cd2b4718f9aa7be34412f537bce6873">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/missing" file 755 mtime 1755033521 "a9865db4f39574ff128c0312c367f070d20f81847817021ecce95fd70a610c9d">,
      #splice.entry<"/build/patchelf-0.15.2/build-aux/test-driver" file 755 mtime 1755033521 "08ff8986c3a0ce8bf32d25ec7e4752ffbce9c89589f018836c97a08b6573ac2c">
  ]
  %3 = splice.literal "glue" at 1669 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960000 "cc9c254c6fd567e67731badfb4f9d3be527c18ea0c8f162500ace4f7eb970e45">
  ]
  %4 = splice.exec "find|basename|stat|mktemp|+4" at 1936(%0#0, %1#0, %1#12) -> [
      #splice.entry<"/build/patchelf-0.15.2/configure" file 755 mtime 1755033523 "3c7e4d98f54f4e76f338969395d44179c719cbda2ca61b147a1e7e915c6976fe">
  ]
  %5:5 = splice.exec "configure" at 2066(%0#0, %0#1, %0#2, %3, %1#0, %1#1, %1#2, %1#3, %1#4, %1#5, %1#6, %2#0, %2#1, %1#9, %2#2, %2#3, %4, %1#13, %1#14, %1#15, %1#16, %1#17, %1#18, %1#19, %1#20, %1#21, %1#22, %1#23, %1#24, %1#25, %1#26, %1#27, %1#28, %1#29, %1#30, %1#31, %1#32, %1#33, %1#34, %1#35, %1#36, %1#37, %1#38, %1#39, %1#40, %1#41, %1#42, %1#43, %1#44, %1#45, %1#46, %1#47, %1#48, %1#49, %1#50, %1#51, %1#52, %1#53, %1#54, %1#55, %1#56, %1#57, %1#58, %1#59, %1#60, %1#61, %1#62, %1#63, %1#64, %1#65, %1#66, %1#67, %1#68, %1#69, %1#70, %1#71, %1#72, %1#73, %1#74, %1#75, %1#76, %1#77, %1#78, %1#79, %1#80, %1#81, %1#82, %1#83, %1#84, %1#85, %1#86, %1#87, %1#88, %1#89, %1#90, %1#91, %1#92, %1#93, %1#94, %1#95, %1#96, %1#97, %1#98, %1#99, %1#100, %1#101, %1#102, %1#103) -> [
      #splice.entry<"/build/patchelf-0.15.2/Makefile" file 644 mtime 1794960001 "802eb805bc7b1c698b1c090d3a384ce53fcd19ce1aa4ef3d43a0430302489bbc">,
      #splice.entry<"/build/patchelf-0.15.2/config.status" file 755 mtime 1794960001 "33833ffc16b967e1e2b3748f3492d2c1728f07a3a94cf5f4d6ad55c52af14ea1">,
      #splice.entry<"/build/patchelf-0.15.2/patchelf.spec" file 644 mtime 1794960001 "5550d9d1a83ef9a7c457499226b359f3b15bda64061d6d6ac69a74fb38ec9bcd">,
      #splice.entry<"/build/patchelf-0.15.2/src/Makefile" file 644 mtime 1794960001 "9cae25c0d7728348be938919b611546f7cd243fc2f732455660393389dc43d83">,
      #splice.entry<"/build/patchelf-0.15.2/tests/Makefile" file 644 mtime 1794960001 "9a5c7e2e39d4f9de3d1f96f24cb460eb33ddaad4617861d9113ca806c75009de">
  ]
  %6 = splice.literal "configure" at 2066 demoted -> [
      #splice.entry<"/build/patchelf-0.15.2/config.log" file 644 mtime 1794960001 "9cfdae2490c45570aa6d553a59e33771897a9eb7d3afdcecd6fabfa68547e10e">
  ]
  %7 = splice.literal "glue" at 9155 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960001 "ced46e55453e2a9718c88770a187c76c10fe8d3c7a91b792cc81d6a625d1ce0d">
  ]
  %8:2 = splice.exec "make" at 9216(%0#0, %0#1, %0#2, %7, %1#0, %1#1, %5#0, %1#2, %1#3, %1#4, %1#5, %1#6, %2#0, %2#1, %1#9, %2#2, %2#3, %6, %5#1, %4, %1#13, %1#14, %1#15, %1#16, %5#2, %1#18, %1#19, %5#3, %1#20, %1#21, %1#22, %1#23, %1#24, %1#25, %5#4, %1#26, %1#27, %1#28, %1#29, %1#30, %1#31, %1#32, %1#33, %1#34, %1#35, %1#36, %1#37, %1#38, %1#39, %1#40, %1#41, %1#42, %1#43, %1#44, %1#45, %1#46, %1#47, %1#48, %1#49, %1#50, %1#51, %1#52, %1#53, %1#54, %1#55, %1#56, %1#57, %1#58, %1#59, %1#60, %1#61, %1#62, %1#63, %1#64, %1#65, %1#66, %1#67, %1#68, %1#69, %1#70, %1#71, %1#72, %1#73, %1#74, %1#75, %1#76, %1#77, %1#78, %1#79, %1#80, %1#81, %1#82, %1#83, %1#84, %1#85, %1#86, %1#87, %1#88, %1#89, %1#90, %1#91, %1#92, %1#93, %1#94, %1#95, %1#96, %1#97, %1#98, %1#99, %1#100, %1#101, %1#102, %1#103) -> [
      #splice.entry<"/build/patchelf-0.15.2/src/patchelf" file 755 mtime 1794960008 "7f42ad1748c066cc8538feed80579fda0ce65341a112a7eea683585dc8b1d0a3">,
      #splice.entry<"/build/patchelf-0.15.2/src/patchelf.o" file 644 mtime 1794960007 "c81db2b85d5cc288e003201b061ea295f4f0e527166e2550de9f3106c2b3bf73">
  ]
  %9 = splice.literal "glue" at 9760 -> [
      #splice.entry<"/build/env-vars" file 600 mtime 1794960008 "ced46e55453e2a9718c88770a187c76c10fe8d3c7a91b792cc81d6a625d1ce0d">
  ]
  %10 = splice.exec "mkdir" at 9780(%0#0, %1#0) -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2" dir 755 mtime 1794960008>
  ]
  %11:9 = splice.exec "make" at 9838(%0#0, %0#1, %0#2, %9, %1#0, %1#1, %5#0, %1#2, %1#3, %1#4, %1#5, %1#6, %2#0, %2#1, %1#9, %2#2, %2#3, %6, %5#1, %4, %1#13, %1#14, %1#15, %1#16, %5#2, %1#18, %1#19, %5#3, %1#20, %1#21, %1#22, %8#0, %1#23, %1#24, %8#1, %1#25, %5#4, %1#26, %1#27, %1#28, %1#29, %1#30, %1#31, %1#32, %1#33, %1#34, %1#35, %1#36, %1#37, %1#38, %1#39, %1#40, %1#41, %1#42, %1#43, %1#44, %1#45, %1#46, %1#47, %1#48, %1#49, %1#50, %1#51, %1#52, %1#53, %1#54, %1#55, %1#56, %1#57, %1#58, %1#59, %1#60, %1#61, %1#62, %1#63, %1#64, %1#65, %1#66, %1#67, %1#68, %1#69, %1#70, %1#71, %1#72, %1#73, %1#74, %1#75, %1#76, %1#77, %1#78, %1#79, %1#80, %1#81, %1#82, %1#83, %1#84, %1#85, %1#86, %1#87, %1#88, %1#89, %1#90, %1#91, %1#92, %1#93, %1#94, %1#95, %1#96, %1#97, %1#98, %1#99, %1#100, %1#101, %1#102, %1#103, %10) -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/bin" dir 755 mtime 1794960008>,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/bin/patchelf" file 755 mtime 1794960008 "7f42ad1748c066cc8538feed80579fda0ce65341a112a7eea683585dc8b1d0a3">,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share" dir 755 mtime 1794960008>,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/doc" dir 755 mtime 1794960008>,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/doc/patchelf" dir 755 mtime 1794960008>,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/doc/patchelf/README.md" file 644 mtime 1794960008 "1914001a74394a6d34a61389de8fa6a1731e6a44a6609a66823467e3a7931091">,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/man" dir 755 mtime 1794960008>,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/man/man1" dir 755 mtime 1794960008>,
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/man/man1/patchelf.1" file 644 mtime 1794960008 "461346ec3323b56674132f919d832d98fbe57f7b10a09596c61d531cab21d013">
  ]
  %12 = splice.exec "find|patchelf" at 10503(%0#0, %1#0, %10, %11#0, %11#1) -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/bin/patchelf" file 755 mtime 1794960008 "f19b0094eea8efb0c63ff7138d3e284bb07b7c0bbadae39a56140b4286d3e438">
  ]
  %13 = splice.exec "find|xargs" at 10711(%0#0, %1#0, %10, %11#2, %11#6, %11#7, %11#8) -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/share/man/man1/patchelf.1.gz" file 644 mtime 1794960008 "68ffe33e5caaa7b592da335ed33d65c2c977227816e1862982f1fd86cc9eec16">
  ]
  %14 = splice.literal "mktemp" at 10799 demoted -> [
      #splice.entry<"/build/striperr.jgN6CC" file 600 mtime 1794960008 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855">
  ]
  %15 = splice.exec "find|sort|cut|xargs" at 10820(%0#0, %1#0, %14, %10, %11#0, %12) -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/bin/patchelf" file 755 mtime 1794960008 "f5d6351c6fc481e6c61c575ef45378e5c585538318eaefd193e8ad7a99f9705f">
  ]
  %16 = splice.exec "mkdir" at 10903(%0#0, %1#0, %10) -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/nix-support" dir 755 mtime 1794960008>
  ]
  %17 = splice.literal "glue" at 10943 -> [
      #splice.entry<"/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2/nix-support/setup-hook" file 644 mtime 1794960008 "7376dede4ff6f66a716e4e5add8b5ec905e8b41c18467c50b40ad779b6c9b497">
  ]
  splice.output "out" "/nix/store/qcwnw71iksl68k7fipizi6wn0s1xms9v-patchelf-0.15.2"(%10, %11#0, %15, %16, %17, %11#2, %11#3, %11#4, %11#5, %11#6, %11#7, %13)
}

