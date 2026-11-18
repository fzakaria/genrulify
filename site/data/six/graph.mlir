module {
  %0 = splice.literal "sandbox" at 716 -> [
      #splice.entry<"/build" dir 700 mtime 1>
  ]
  %1:20 = splice.exec "cp" at 1851(%0) -> [
      #splice.entry<"/build/source" dir 555 mtime 1>,
      #splice.entry<"/build/source/.github" dir 555 mtime 1>,
      #splice.entry<"/build/source/.github/workflows" dir 555 mtime 1>,
      #splice.entry<"/build/source/.github/workflows/ci.yml" file 444 mtime 1 "9f337b07645333d1b4d77ef6132c5b326845510d87678e8fbe3a6d1955b3ea1c">,
      #splice.entry<"/build/source/.github/workflows/publish.yml" file 444 mtime 1 "2f94639add4e00dcfba170fd30ff1164cc0debe6912105c4b20e0aeaa70c79cb">,
      #splice.entry<"/build/source/.gitignore" file 444 mtime 1 "8d1a9fa4202089bc5a9f67a8eb8cae36bbf5c645119e3d977fe5c1229ef38f9b">,
      #splice.entry<"/build/source/CHANGES" file 444 mtime 1 "574d5c41e6f9629d88430f1e6e5245bc0c41911924847a0fe72d9947df770f32">,
      #splice.entry<"/build/source/CONTRIBUTORS" file 444 mtime 1 "037fd27943147457a7be9766f04e4fd25df55918114272c7c1d3d6f195ca3443">,
      #splice.entry<"/build/source/LICENSE" file 444 mtime 1 "4375ba20e2b9c6c4e7cad2940a628fd90e95cc3d50ee92aae755715d8ba1fbd0">,
      #splice.entry<"/build/source/MANIFEST.in" file 444 mtime 1 "e18c7205486527268826a438e7efa721d330d77956f8bfff28b212ddafd7f3d9">,
      #splice.entry<"/build/source/README.rst" file 444 mtime 1 "ca64c34dcb4a98cc79cef7f9cd34bfc939ffcfd9a1b1643121c20e9dae049c97">,
      #splice.entry<"/build/source/documentation" dir 555 mtime 1>,
      #splice.entry<"/build/source/documentation/Makefile" file 444 mtime 1 "43187a18a0486a0c5c60e411a5e492d1726b8e914b2e89363d70af41d1aee346">,
      #splice.entry<"/build/source/documentation/conf.py" file 444 mtime 1 "558e2156c5b11e0ff4b945fe3d3301b463a54de9d237a8f05f271149bc0dd9ef">,
      #splice.entry<"/build/source/documentation/index.rst" file 444 mtime 1 "579d8bb6691a5c93c0ecc7a173af7709931b156dceb06519f6c2b951b0345d95">,
      #splice.entry<"/build/source/setup.cfg" file 444 mtime 1 "efe4f61cc8f3944109c5c192c1ecf587a1555990a48e437a6e3c0df348f9c29c">,
      #splice.entry<"/build/source/setup.py" file 444 mtime 1 "317ace0d106fbffce6d754a2454b0e38b345105f298e8e5614bea48516a268d9">,
      #splice.entry<"/build/source/six.py" file 444 mtime 1 "c51c91f703d3d4b3696c923cb5fec213e05e75d9215393befac7f2fa6a3904df">,
      #splice.entry<"/build/source/test_six.py" file 444 mtime 1 "33f3f18bb5ddfbc6cf5be750677ab6e4e1a6c81cf48a95868ff98fcb5213a932">,
      #splice.entry<"/build/source/tox.ini" file 444 mtime 1 "049a1fc95f83a7aaf6fb0e673a3058b48856c2693b1b96cd81c3efd260939775">
  ]
  %2:20 = splice.exec "chmod" at 1906(%0, %1#0, %1#1, %1#2, %1#3, %1#4, %1#5, %1#6, %1#7, %1#8, %1#9, %1#10, %1#11, %1#12, %1#13, %1#14, %1#15, %1#16, %1#17, %1#18, %1#19) -> [
      #splice.entry<"/build/source" dir 755 mtime 1>,
      #splice.entry<"/build/source/.github" dir 755 mtime 1>,
      #splice.entry<"/build/source/.github/workflows" dir 755 mtime 1>,
      #splice.entry<"/build/source/.github/workflows/ci.yml" file 644 mtime 1 "9f337b07645333d1b4d77ef6132c5b326845510d87678e8fbe3a6d1955b3ea1c">,
      #splice.entry<"/build/source/.github/workflows/publish.yml" file 644 mtime 1 "2f94639add4e00dcfba170fd30ff1164cc0debe6912105c4b20e0aeaa70c79cb">,
      #splice.entry<"/build/source/.gitignore" file 644 mtime 1 "8d1a9fa4202089bc5a9f67a8eb8cae36bbf5c645119e3d977fe5c1229ef38f9b">,
      #splice.entry<"/build/source/CHANGES" file 644 mtime 1 "574d5c41e6f9629d88430f1e6e5245bc0c41911924847a0fe72d9947df770f32">,
      #splice.entry<"/build/source/CONTRIBUTORS" file 644 mtime 1 "037fd27943147457a7be9766f04e4fd25df55918114272c7c1d3d6f195ca3443">,
      #splice.entry<"/build/source/LICENSE" file 644 mtime 1 "4375ba20e2b9c6c4e7cad2940a628fd90e95cc3d50ee92aae755715d8ba1fbd0">,
      #splice.entry<"/build/source/MANIFEST.in" file 644 mtime 1 "e18c7205486527268826a438e7efa721d330d77956f8bfff28b212ddafd7f3d9">,
      #splice.entry<"/build/source/README.rst" file 644 mtime 1 "ca64c34dcb4a98cc79cef7f9cd34bfc939ffcfd9a1b1643121c20e9dae049c97">,
      #splice.entry<"/build/source/documentation" dir 755 mtime 1>,
      #splice.entry<"/build/source/documentation/Makefile" file 644 mtime 1 "43187a18a0486a0c5c60e411a5e492d1726b8e914b2e89363d70af41d1aee346">,
      #splice.entry<"/build/source/documentation/conf.py" file 644 mtime 1 "558e2156c5b11e0ff4b945fe3d3301b463a54de9d237a8f05f271149bc0dd9ef">,
      #splice.entry<"/build/source/documentation/index.rst" file 644 mtime 1 "579d8bb6691a5c93c0ecc7a173af7709931b156dceb06519f6c2b951b0345d95">,
      #splice.entry<"/build/source/setup.cfg" file 644 mtime 1 "efe4f61cc8f3944109c5c192c1ecf587a1555990a48e437a6e3c0df348f9c29c">,
      #splice.entry<"/build/source/setup.py" file 644 mtime 1 "317ace0d106fbffce6d754a2454b0e38b345105f298e8e5614bea48516a268d9">,
      #splice.entry<"/build/source/six.py" file 644 mtime 1 "c51c91f703d3d4b3696c923cb5fec213e05e75d9215393befac7f2fa6a3904df">,
      #splice.entry<"/build/source/test_six.py" file 644 mtime 1 "33f3f18bb5ddfbc6cf5be750677ab6e4e1a6c81cf48a95868ff98fcb5213a932">,
      #splice.entry<"/build/source/tox.ini" file 644 mtime 1 "049a1fc95f83a7aaf6fb0e673a3058b48856c2693b1b96cd81c3efd260939775">
  ]
  %3:16 = splice.exec "find" at 1934(%0, %2#0, %2#1, %2#2, %2#3, %2#4, %2#5, %2#6, %2#7, %2#8, %2#9, %2#10, %2#11, %2#12, %2#13, %2#14, %2#15, %2#16, %2#17, %2#18, %2#19) -> [
      #splice.entry<"/build/source/.github/workflows/ci.yml" file 644 mtime 315619200 "9f337b07645333d1b4d77ef6132c5b326845510d87678e8fbe3a6d1955b3ea1c">,
      #splice.entry<"/build/source/.github/workflows/publish.yml" file 644 mtime 315619200 "2f94639add4e00dcfba170fd30ff1164cc0debe6912105c4b20e0aeaa70c79cb">,
      #splice.entry<"/build/source/.gitignore" file 644 mtime 315619200 "8d1a9fa4202089bc5a9f67a8eb8cae36bbf5c645119e3d977fe5c1229ef38f9b">,
      #splice.entry<"/build/source/CHANGES" file 644 mtime 315619200 "574d5c41e6f9629d88430f1e6e5245bc0c41911924847a0fe72d9947df770f32">,
      #splice.entry<"/build/source/CONTRIBUTORS" file 644 mtime 315619200 "037fd27943147457a7be9766f04e4fd25df55918114272c7c1d3d6f195ca3443">,
      #splice.entry<"/build/source/LICENSE" file 644 mtime 315619200 "4375ba20e2b9c6c4e7cad2940a628fd90e95cc3d50ee92aae755715d8ba1fbd0">,
      #splice.entry<"/build/source/MANIFEST.in" file 644 mtime 315619200 "e18c7205486527268826a438e7efa721d330d77956f8bfff28b212ddafd7f3d9">,
      #splice.entry<"/build/source/README.rst" file 644 mtime 315619200 "ca64c34dcb4a98cc79cef7f9cd34bfc939ffcfd9a1b1643121c20e9dae049c97">,
      #splice.entry<"/build/source/documentation/Makefile" file 644 mtime 315619200 "43187a18a0486a0c5c60e411a5e492d1726b8e914b2e89363d70af41d1aee346">,
      #splice.entry<"/build/source/documentation/conf.py" file 644 mtime 315619200 "558e2156c5b11e0ff4b945fe3d3301b463a54de9d237a8f05f271149bc0dd9ef">,
      #splice.entry<"/build/source/documentation/index.rst" file 644 mtime 315619200 "579d8bb6691a5c93c0ecc7a173af7709931b156dceb06519f6c2b951b0345d95">,
      #splice.entry<"/build/source/setup.cfg" file 644 mtime 315619200 "efe4f61cc8f3944109c5c192c1ecf587a1555990a48e437a6e3c0df348f9c29c">,
      #splice.entry<"/build/source/setup.py" file 644 mtime 315619200 "317ace0d106fbffce6d754a2454b0e38b345105f298e8e5614bea48516a268d9">,
      #splice.entry<"/build/source/six.py" file 644 mtime 315619200 "c51c91f703d3d4b3696c923cb5fec213e05e75d9215393befac7f2fa6a3904df">,
      #splice.entry<"/build/source/test_six.py" file 644 mtime 315619200 "33f3f18bb5ddfbc6cf5be750677ab6e4e1a6c81cf48a95868ff98fcb5213a932">,
      #splice.entry<"/build/source/tox.ini" file 644 mtime 315619200 "049a1fc95f83a7aaf6fb0e673a3058b48856c2693b1b96cd81c3efd260939775">
  ]
  %4:13 = splice.exec "pyproject-build" at 2660(%0, %2#0, %3#5, %3#6, %3#7, %3#11, %3#12, %3#13) -> [
      #splice.entry<"/build/source/__pycache__" dir 755 mtime 1794960000>,
      #splice.entry<"/build/source/__pycache__/six.cpython-314.pyc" file 644 mtime 1794960000 "6443e28a88e8807834ec0f703ba24efdb035bfb97742f9eab26175175911a36b">,
      #splice.entry<"/build/source/build" dir 755 mtime 1794960000>,
      #splice.entry<"/build/source/build/bdist.linux-x86_64" dir 755 mtime 1794960000>,
      #splice.entry<"/build/source/build/lib" dir 755 mtime 1794960000>,
      #splice.entry<"/build/source/build/lib/six.py" file 644 mtime 315619200 "c51c91f703d3d4b3696c923cb5fec213e05e75d9215393befac7f2fa6a3904df">,
      #splice.entry<"/build/source/dist" dir 755 mtime 1794960000>,
      #splice.entry<"/build/source/dist/six-1.17.0-py2.py3-none-any.whl" file 644 mtime 1794960000 "a8c502c8460b4bfd1a1cb549889b02ee89ca75974d25eaa3407cd6e70e6c49db">,
      #splice.entry<"/build/source/six.egg-info" dir 755 mtime 1794960000>,
      #splice.entry<"/build/source/six.egg-info/PKG-INFO" file 644 mtime 1794960000 "ac67d2c2d38c83f38e4928241fc1d4ffd7a139b8a87dcdfefa43178af4f2c0f5">,
      #splice.entry<"/build/source/six.egg-info/SOURCES.txt" file 644 mtime 1794960000 "0e5cf78d9cf118b109b87e8640636cebaedaa7bdc22a3be9fd6fcbe8ea43dc8e">,
      #splice.entry<"/build/source/six.egg-info/dependency_links.txt" file 644 mtime 1794960000 "01ba4719c80b6fe911b091a7c05124b64eeece964e09c058ef8f9805daca546b">,
      #splice.entry<"/build/source/six.egg-info/top_level.txt" file 644 mtime 1794960000 "fe2547fe2604b445e70fc9d819062960552f9145bdb043b51986e478a4806a2b">
  ]
  %5:15 = splice.exec "python3.14" at 8691(%0, %2#0, %4#6, %4#7) -> [
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/__pycache__" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/__pycache__/six.cpython-314.opt-1.pyc" file 644 mtime 1794960000 "24ec43c37143c3331d8d20bca9cc915b94d1e0d2ea70e6744eae20300bdfddd8">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/__pycache__/six.cpython-314.pyc" file 644 mtime 1794960000 "24ec43c37143c3331d8d20bca9cc915b94d1e0d2ea70e6744eae20300bdfddd8">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info/METADATA" file 644 mtime 1794960000 "ac67d2c2d38c83f38e4928241fc1d4ffd7a139b8a87dcdfefa43178af4f2c0f5">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info/RECORD" file 644 mtime 1794960000 "a60e370f9861ecca7bffc1d7ece399a5347b4461a42ba15a9351d483424d3976">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info/WHEEL" file 644 mtime 1794960000 "a96107396797a1ff6424b5c05c9d61ba443f32f56d695f545007b0e8ded8f7d1">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info/licenses" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info/licenses/LICENSE" file 644 mtime 1794960000 "4375ba20e2b9c6c4e7cad2940a628fd90e95cc3d50ee92aae755715d8ba1fbd0">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six-1.17.0.dist-info/top_level.txt" file 644 mtime 1794960000 "fe2547fe2604b445e70fc9d819062960552f9145bdb043b51986e478a4806a2b">,
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/lib/python3.14/site-packages/six.py" file 644 mtime 1794960000 "c51c91f703d3d4b3696c923cb5fec213e05e75d9215393befac7f2fa6a3904df">
  ]
  %6:2 = splice.exec "mv" at 9656(%0, %2#0, %4#6, %4#7) -> [
      #splice.entry<"/nix/store/xdyfansxkvyki35nzivbxd5pcvcx09vg-python3.14-six-1.17.0-dist" dir 755 mtime 1794960000>,
      #splice.entry<"/nix/store/xdyfansxkvyki35nzivbxd5pcvcx09vg-python3.14-six-1.17.0-dist/six-1.17.0-py2.py3-none-any.whl" file 644 mtime 1794960000 "a8c502c8460b4bfd1a1cb549889b02ee89ca75974d25eaa3407cd6e70e6c49db">
  ]
  %7 = splice.exec "mkdir" at 10945(%0, %2#0, %5#0) -> [
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/nix-support" dir 755 mtime 1794960000>
  ]
  %8 = splice.literal "glue" at 10973 -> [
      #splice.entry<"/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0/nix-support/propagated-build-inputs" file 644 mtime 1794960000 "95dc30ceb9f93bf2cf34a9d0d0b87631485960de6787b2688c99aaddbd967fd4">
  ]
  splice.output "dist" "/nix/store/xdyfansxkvyki35nzivbxd5pcvcx09vg-python3.14-six-1.17.0-dist"(%6#0, %6#1)
  splice.output "out" "/nix/store/ryg0sjdnp2vhi51m876d5blj7si80mgf-python3.14-six-1.17.0"(%5#0, %5#1, %5#2, %5#3, %5#4, %5#5, %5#6, %5#7, %5#8, %5#9, %5#10, %5#11, %5#12, %5#13, %5#14, %7, %8)
}

