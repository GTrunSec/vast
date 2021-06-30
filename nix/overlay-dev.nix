final: prev:
let
  inherit (final) lib;
  inherit (final.stdenv.hostPlatform) isStatic;
  stdenv = if prev.stdenv.isDarwin then final.llvmPackages_10.stdenv else final.stdenv;
in
{

  musl = prev.musl.overrideAttrs (old: {
    CFLAGS = old.CFLAGS ++ [ "-fno-omit-frame-pointer" ];
  });

  arrow-cpp = (prev.arrow-cpp.override { enableShared = !isStatic; }).overrideAttrs (old: {
    cmakeFlags = old.cmakeFlags ++ [
      "-DARROW_CXXFLAGS=-fno-omit-frame-pointer"
    ];
  });
  arrow-cpp-no-simd = (prev.arrow-cpp.override { enableShared = !isStatic; }).overrideAttrs (old: {
    cmakeFlags = old.cmakeFlags ++ [
      "-DARROW_CXXFLAGS=-fno-omit-frame-pointer"
      "-DARROW_SIMD_LEVEL=NONE"
    ];
  });

  xxHash = if !isStatic then prev.xxHash else
  prev.xxHash.overrideAttrs (old: {
    patches = [ ./xxHash/static.patch ];
  });

  jemalloc = prev.jemalloc.overrideAttrs (old: {
    EXTRA_CFLAGS = (old.EXTRA_CFLAGS or "") + " -fno-omit-frame-pointer";
    configureFlags = old.configureFlags ++ [ "--enable-prof" "--enable-stats" ];
  });

  simdjson = prev.simdjson.overrideAttrs (old: {
    cmakeFlags = old.cmakeFlags ++ lib.optionals isStatic [
      "-DSIMDJSON_BUILD_STATIC=ON"
    ];
  });

  spdlog = prev.spdlog.overrideAttrs (old: {
    cmakeFlags = old.cmakeFlags ++ lib.optionals isStatic [
      "-DSPDLOG_BUILD_STATIC=ON"
      "-DSPDLOG_BUILD_SHARED=OFF"
    ];
  });
}
