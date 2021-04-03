{
  inputs = {
    broker = { url = "github:zeek/broker/e65cca620b1d2d26af6c5a9ee9513321fc3ed2a0"; flake = false; };
    nixpkgs.url = "nixpkgs/449b698a0b554996ac099b4e3534514528019269";
    caf = { url = "github:actor-framework/actor-framework/347917fee3420d5fa02220af861869d1728b7fc0"; flake = false; };
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = inputs@{ self, nixpkgs, broker, caf, flake-utils }:
    flake-utils.lib.eachDefaultSystem
      (
        system:
        let
          pkgs = import nixpkgs {
            inherit system; overlays = [
            self.overlay
            (import ./nix/overlay.nix)
          ];
          };
        in
        with pkgs;
        rec {
          packages = flake-utils.lib.flattenTree {
            vast = pkgs.vast;
          };

          defaultPackage = packages.vast;

          hydraJobs = {
            inherit packages;
          };

          devShell = with pkgs; mkShell {
            buildInputs = [ vast ];
            shellHook = ''
              gitVersion=$(git describe --tags --long --dirty)
              sed -i 's|versionOverride = "2021.03.25-0-gb8ed41cd8-dirty";|versionOverride = "'"$gitVersion"'";|' flake.nix
            '';
          };
        }
      ) // {
      overlay = final: prev:
        let
          stdenv = if prev.stdenv.isDarwin then final.llvmPackages_10.stdenv else final.stdenv;
          versionOverride = "2021.03.25-0-gb8ed41cd8-dirty";
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
          vast = final.callPackage ./nix/vast {
            inherit versionOverride stdenv;
          };
        };

      nixosModules.vast = { lib, pkgs, config, ... }:
        with lib;
        let
          cfg = config.services.vast;
          configFile = pkgs.writeText "vast.conf" (
            builtins.toJSON {
              vast = {
                endpoint = "${toString cfg.endpoint}";
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
                  default = self.outputs.defaultPackage.x86_64-linux;
                  description = "The vast package.";
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
                ExecReload = "${pkgs.coreutils}/bin/kill -HUP $MAINPID && ${pkgs.coreutils}/bin/rm vast.db/pid.lock";
                User = "vast";
                WorkingDirectory = "/var/lib/vast";
                ReadWritePaths = "/var/lib/vast";
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
