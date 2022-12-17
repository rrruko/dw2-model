let
  nixpkgs =
    import (builtins.fetchTarball {
      name = "nixos-21.11";
      url =
      "https://github.com/nixos/nixpkgs/archive/fd43ce017d4c95f47166d28664a004f57458a0b1.tar.gz";
      sha256 = "1lkc5nbl53wlqp9hv03jlh7yyqn02933xdii5q7kbn49rplrx619";
    }) {};
in
nixpkgs.stdenv.mkDerivation {
  name = "dw2-model";
  src = ./.;
  buildInputs = [ nixpkgs.libpng ];
  buildPhase = ''
    gcc matrix.c rip_model.c iso_reader.c -lpng -lm -Wall -g -I . -o rip_model
    gcc iso_reader.c index_files.c -Wall -I . -o index_files
  '';
  installPhase = ''
    mkdir $out
    cp rip_model $out/
    cp index_files $out/
  '';
}
