final: prev:
let
  stdenv = if prev.stdenv.isDarwin then final.llvmPackages_10.stdenv else final.stdenv;
in
{
  caf-vast = with prev; with final.stdenv.hostPlatform;
    let
      source = builtins.fromJSON (builtins.readFile ./caf/source.json);
    in
    (prev.caf.override { inherit stdenv; }).overrideAttrs (old: {
      # fetchFromGitHub uses ellipsis in the parameter set to be hash method
      # agnostic. Because of that, callPackageWith does not detect that sha256
      # is a required argument, and it has to be passed explicitly instead.
      src = lib.callPackageWith source final.fetchFromGitHub { inherit (source) sha256; };
      inherit (source) version;
      NIX_CFLAGS_COMPILE = "-fno-omit-frame-pointer"
        # Building statically implies using -flto. Since we produce a final binary with
        # link time optimizaitons in VAST, we need to make sure that type definitions that
        # are parsed in both projects are the same, otherwise the compiler will complain
        # at the optimization stage.
        # TODO: Remove when updating to CAF 0.18.
        + lib.optionalString isStatic "-std=c++17";
    } // lib.optionalAttrs isStatic {
      cmakeFlags = old.cmakeFlags ++ [
        "-DCAF_BUILD_STATIC=ON"
        "-DCAF_BUILD_STATIC_ONLY=ON"
        "-DOPENSSL_USE_STATIC_LIBS=TRUE"
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION:BOOL=ON"
        "-DCMAKE_POLICY_DEFAULT_CMP0069=NEW"
      ];
      hardeningDisable = [
        "pic"
      ];
      dontStrip = true;
    });
  vast-source = final.nix-gitignore.gitignoreSource [ ] ./..;
  vast-gitDescribe = final.callPackage ./gitDescribe.nix { };
  vast = final.callPackage ./vast { inherit stdenv; caf = final.caf-vast; };
  vast-ci = final.vast.override {
    buildType = "CI";
    arrow-cpp = final.arrow-cpp-no-simd;
  };
}
