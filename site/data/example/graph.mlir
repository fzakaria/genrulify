module {
  %0 = splice.literal "sandbox" at 717 -> [
      #splice.entry<"/build" dir 700 mtime 1>
  ]
  %1 = splice.exec "dd" at 735(%0) -> [
      #splice.entry<"/build/bundle" file 644 mtime 1 "b08e44f9eb798c1413c3e066dc5ccfd2633b760feb1215a2e6b6c4e2001618a4">
  ]
  %2 = splice.exec "dd" at 764(%0, %1) -> [
      #splice.entry<"/build/bundle" file 644 mtime 1 "c82437d39e10d7a564830c757b91784a1d110f5665978f672a1b4119c2ef2b5e">
  ]
  %3 = splice.exec "dd" at 782(%0, %2) -> [
      #splice.entry<"/nix/store/r8mgc2hqpzggmvmscm94dynii1wk1ygm-nixsplice-example" file 644 mtime 1 "c82437d39e10d7a564830c757b91784a1d110f5665978f672a1b4119c2ef2b5e">
  ]
  splice.output "out" "/nix/store/r8mgc2hqpzggmvmscm94dynii1wk1ygm-nixsplice-example"(%3)
}

