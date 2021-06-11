{
  inputs = {
    zeek-vast-src = { url = "github:tenzir/zeek-vast"; flake = false; };
    nixpkgs.url = "nixpkgs/449b698a0b554996ac099b4e3534514528019269";
    caf = { url = "github:actor-framework/actor-framework/347917fee3420d5fa02220af861869d1728b7fc0"; flake = false; };
    flake-utils.url = "github:numtide/flake-utils";
    nixpkgs-hardenedlinux = { url = "github:hardenedlinux/nixpkgs-hardenedlinux"; };
  };

  outputs = inputs: with builtins;
    with inputs;
    flake-utils.lib.eachDefaultSystem
      (
        system:
        let
          pkgs = import nixpkgs {
            inherit system; overlays = [
            self.overlay
            self.overlay-extend
            nixpkgs-hardenedlinux.overlay
          ];
            config = { allowBroken = true; };
          };
        in
        with pkgs;
        rec {
          packages = flake-utils.lib.flattenTree
            {
              vast = pkgs.vast;
              pyvast = pkgs.pyvast;
              zeek-vast = pkgs.zeek-vast;
            };

          defaultPackage = packages.vast;

          hydraJobs = {
            inherit packages;
          };

          devShell = with pkgs; mkShell {
            buildInputs = [ btest ];
            shellHook = ''
            '';
          };
        }
      ) // {
      overlay-extend = import ./nix/overlay.nix;
      overlay = final: prev:
        let
          stdenv = if prev.stdenv.isDarwin then final.llvmPackages_10.stdenv else final.stdenv;
          versionOverride = "2021.05.27-6-ge6a33569e-dirty";
          # version = with prev; builtins.readFile
          #   (runCommandLocal "gitDescribe.out"
          #     {
          #       nativeBuildInputs = [ git ];
          #     } ''
          #     cd ${self}
          #     echo -n $(git describe --tags --long --dirty) > $out
          #   '');
        in
        {
          zeek-vast = final.vast.overrideAttrs (old: rec {
            preConfigure = (old.preConfigure or "") + ''
              ln -s ${zeek-vast-src}/zeek-to-vast tools/.
              echo "add_subdirectory(zeek-to-vast)" >> tools/CMakeLists.txt
            '';
            cmakeFlags = (old.cmakeFlags or [ ]) ++ [
              "-DBROKER_ROOT_DIR=${final.broker}"
            ];
          });

          pyvast = with final;
            (python3Packages.buildPythonPackage {
              pname = "pyvast";
              version = versionOverride;
              src = ./pyvast;
              doCheck = false;
              propagatedBuildInputs = with python3Packages; [
                aiounittest
              ];
            });

          vast = final.callPackage ./nix/vast {
            inherit versionOverride stdenv;
          };

          cmake-format = prev.cmake-format.overrideAttrs
            (old: {
              buildInputs = (old.buildInputs or [ ]) ++ [
                prev.python3Packages.six
              ];
            });
        };

      nixosModules.vast = { lib, pkgs, config, ... }:
        with lib;
        let
          cfg = config.services.vast;
          configFile = pkgs.writeText "vast.yaml"
            (
              builtins.toJSON {
                vast = {
                  endpoint = cfg.endpoint;
                  db-directory = cfg.dataDir;
                } // cfg.settings;
              });
        in
        {
          options =
            {
              services.vast = {
                enable = mkOption {
                  type = types.bool;
                  default = false;
                  description = ''
                    Whether to enable vast endpoint
                  '';
                };

                settings = mkOption {
                  type = types.attrsOf types.anything;
                  default = { };
                };

                package = mkOption {
                  type = types.package;
                  default = self.outputs.packages."${pkgs.system}".vast;
                  description = "The vast package.";
                };

                dataDir = mkOption {
                  type = types.str;
                  default = "/var/lib/vast";
                  description = "The file system path used for persistent state.";
                };

                endpoint = mkOption {
                  type = types.str;
                  # if the confinement is enable, the localhost does not working anymore
                  default = "127.0.0.1:4000";
                  example = "localhost:4000";
                  description = ''
                    The host and port to listen at and connect to.
                  '';
                };
              };
            };

          config = mkIf cfg.enable {
            users.users.vast =
              { isSystemUser = true; group = "vast"; };

            users.groups.vast = { };

            systemd.services.vast = {
              enable = true;
              description = "Visibility Across Space and Time";
              wantedBy = [ "multi-user.target" ];

              after = [
                "network-online.target"
                #"zeek.service
              ];

              confinement = {
                enable = true;
                binSh = null;
              };

              script = ''
                exec ${cfg.package}/bin/vast --config=${configFile} start
              '';

              serviceConfig = {
                Restart = "always";
                RestartSec = "10";
                ExecReload = "${pkgs.coreutils}/bin/kill -HUP $MAINPID && ${pkgs.coreutils}/bin/rm ${cfg.dataDir}/vast.db/pid.lock";
                User = "vast";
                WorkingDirectory = cfg.dataDir;
                ReadWritePaths = cfg.dataDir;
                RuntimeDirectory = "vast";
                CacheDirectory = "vast";
                StateDirectory = "vast";
                SyslogIdentifier = "vast";
                PrivateUsers = true;
                DynamicUser = mkForce false;
                PrivateTmp = true;
                ProtectHome = true;
                PrivateDevices = true;
                ProtectKernelTunables = true;
                ProtectControlGroups = true;
                ProtectKernelModules = true;
                ProtectKernelLogs = true;
              };
            };
          };
        };
    };
}
