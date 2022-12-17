{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation {
  name = "dw2-model";
  src = ./.;
  buildInputs = [ pkgs.libpng ];
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
