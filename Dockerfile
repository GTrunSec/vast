ARG DOCKER_BUILD=build

# build image, install build-dependencies and build vast
FROM debian:buster-backports as build
LABEL builder=true
LABEL maintainer="engineering@tenzir.com"

ENV PREFIX /usr/local
ENV CC gcc-8
ENV CXX g++-8
ENV BUILD_TYPE Release
ENV BUILD_DIR /tmp/src

# Compiler and dependency setup
RUN apt-get -qq update && apt-get -qqy install \
  build-essential gcc-8 g++-8 ninja-build \
  ca-certificates \
  flatbuffers-compiler-dev \
  git-core \
  gnupg2 \
  jq \
  libflatbuffers-dev \
  libpcap-dev tcpdump \
  libsimdjson-dev \
  libssl-dev \
  libunwind-dev \
  libyaml-cpp-dev \
  lsb-release \
  pkg-config \
  python3-dev \
  python3-pip \
  wget \
  python3-venv

# Need to specify backports explicitly, since spdlog and fmt also have regular
# buster packages. Also, this comes with a newer version of CMake.
RUN apt-get -qqy -t buster-backports install cmake libspdlog-dev libfmt-dev

# Apache Arrow (c.f. https://arrow.apache.org/install/)
RUN wget https://apache.jfrog.io/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-archive-keyring-latest-$(lsb_release --codename --short).deb && \
  apt-get -qqy install ./apache-arrow-archive-keyring-latest-$(lsb_release --codename --short).deb && \
  sed -i'' -e 's,https://apache.bintray.com/,https://apache.jfrog.io/artifactory/,g' /etc/apt/sources.list.d/apache-arrow.sources && \
  apt-get -qq update && \
  apt-get -qqy install libarrow-dev

# VAST
WORKDIR $BUILD_DIR/vast
COPY changelog ./changelog
COPY cmake ./cmake
COPY doc ./doc
COPY examples ./examples
COPY libvast ./libvast
COPY libvast_test ./libvast_test
COPY plugins ./plugins
COPY schema ./schema
COPY scripts ./scripts
COPY tools ./tools
COPY vast ./vast
COPY .clang-format .cmake-format LICENSE LICENSE.3rdparty README.md BANNER CHANGELOG.md CMakeLists.txt vast.yaml.example ./

# Resolve repository-internal symlinks.
RUN ln -sf ../../pyvast/pyvast examples/jupyter/pyvast && \
  ln -sf ../../vast.yaml.example vast/integration/vast.yaml.example && \
  ln -sf ../../vast/integration/data/ plugins/pcap/data/ && \
  ln -sf ../vast/integration/misc/scripts/print-arrow.py scripts/print-arrow.py

RUN cmake -B build -G Ninja \
    -DCMAKE_INSTALL_PREFIX:STRING="$PREFIX" \
    -DCMAKE_BUILD_TYPE:STRING="$BUILD_TYPE" \
    -DVAST_PLUGINS:STRING="plugins/pcap"
RUN cmake --build build --parallel
# --test-dir is not yet supported in the ctest version we're using here.
RUN CTEST_OUTPUT_ON_FAILURE=1 cd build && ctest --parallel
RUN cmake --install build
RUN cmake --build build --target integration

# copy prebuilt image
FROM debian:buster-backports as prebuilt
ENV PREFIX /usr/local

RUN apt-get -qq update && apt-get -qqy install rsync

COPY opt/vast /opt/vast
RUN rsync -avh /opt/vast/* $PREFIX

# "Hack": Docker does not allow to use COPY --from=${BUILD_ARG}, but we can name
# a build stage after the contents of a build-arg and then use the stage as
# argument in COPY --from="stage"
FROM $DOCKER_BUILD as build_type

# Production image: copy application
FROM debian:buster-backports
ENV PREFIX /usr/local

COPY --from=build_type $PREFIX/ $PREFIX/
RUN apt-get -qq update && apt-get -qq install -y libc++1 libc++abi1 libpcap0.8 \
  openssl libsimdjson4 libyaml-cpp0.6 libasan5 libflatbuffers1 wget gnupg2 \
  lsb-release ca-certificates libunwind8

# Need to specify backports explicitly, since spdlog and fmt also have regular
# buster packages. For fmt we install the dev package, because libfmt is only
# packaged for bullseye:
# https://packages.debian.org/search?keywords=libfmt&searchon=names&suite=all&section=all
RUN apt-get -qqy -t buster-backports install libspdlog1 libfmt-dev

# Apache Arrow (c.f. https://arrow.apache.org/install/)
RUN wget https://apache.jfrog.io/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-archive-keyring-latest-$(lsb_release --codename --short).deb && \
  apt-get -qqy install ./apache-arrow-archive-keyring-latest-$(lsb_release --codename --short).deb && \
  sed -i'' -e 's,https://apache.bintray.com/,https://apache.jfrog.io/artifactory/,g' /etc/apt/sources.list.d/apache-arrow.sources && \
  apt-get -qq update && \
  apt-get -qqy install libarrow-dev

EXPOSE 42000/tcp

RUN echo "Adding vast user" && useradd --system --user-group vast

RUN mkdir -p $PREFIX/etc/vast /var/log/vast /var/lib/vast
COPY systemd/vast.yaml $PREFIX/etc/vast/vast.yaml
RUN chown -R vast:vast /var/log/vast /var/lib/vast

WORKDIR /var/lib/vast
VOLUME ["/var/lib/vast"]

USER vast:vast
ENTRYPOINT ["vast"]
CMD ["--help"]
