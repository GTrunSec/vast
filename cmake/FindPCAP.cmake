# Tries to find libpcap headers and libraries
#
# Usage of this module as follows:
#
#     find_package(PCAP)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  PCAP_ROOT_DIR  Set this variable to the root installation of
#                 libpcap if the module has problems finding
#                 the proper installation path.
#
# Variables defined by this module:
#
#  PCAP_FOUND              System has PCAP libs/headers
#  PCAP_LIBRARIES          The PCAP libraries
#  PCAP_INCLUDE_DIR        The location of PCAP headers

find_package(PkgConfig)

if (PkgConfig_FOUND)
  pkg_check_modules(PCAP IMPORTED_TARGET libpcap)
  if (PCAP_FOUND AND NOT TARGET pcap::pcap)
    add_library(pcap::pcap ALIAS PkgConfig::PCAP)
  endif ()
endif ()

if (NOT PCAP_FOUND)
  find_path(PCAP_INCLUDE_DIR
    NAMES pcap.h
    HINTS ${PCAP_ROOT_DIR}/include)

  find_library(PCAP_LIBRARIES
    NAMES pcap
    HINTS ${PCAP_ROOT_DIR}/lib)

  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(
    PCAP
    DEFAULT_MSG
    PCAP_LIBRARIES
    PCAP_INCLUDE_DIR)

  mark_as_advanced(
    PCAP_ROOT_DIR
    PCAP_LIBRARIES
    PCAP_INCLUDE_DIR)

  # create IMPORTED target for libpcap dependency
  if (PCAP_FOUND AND NOT TARGET pcap::pcap)
    add_library(pcap::pcap UNKNOWN IMPORTED GLOBAL)
    set_target_properties(pcap::pcap PROPERTIES
      IMPORTED_LOCATION ${PCAP_LIBRARIES})
  endif ()
endif ()
