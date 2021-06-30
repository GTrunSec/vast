let
  nixpkgs_ = import ./pinned.nix;
  vastPkgs = import ./overlay.nix;
  vastDev = import ./overlay-dev.nix;
in

{ nixpkgs ? nixpkgs_ }:
(import nixpkgs { config = { }; overlays = [ vastPkgs vastDev ]; }) //
{
  inherit nixpkgs_;
}
