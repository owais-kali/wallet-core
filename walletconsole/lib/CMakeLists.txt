# SPDX-License-Identifier: Apache-2.0
#
# Copyright © 2017 Trust Wallet.

# walletconsolelib library
file(GLOB_RECURSE walletconsolelib_sources *.cpp)
add_library(walletconsolelib ${walletconsolelib_sources})
#target_link_libraries(tests gtest_main TrezorCrypto TrustWalletCore protobuf Boost::boost)
target_link_libraries(walletconsolelib TrezorCrypto TrustWalletCore protobuf Boost::boost)
target_include_directories(walletconsolelib PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)

# Optional line editing (tab completion of commands and BIP39 words) via libedit
find_path(EDITLINE_INCLUDE_DIR editline/readline.h)
find_library(EDITLINE_LIBRARY NAMES edit)
if (EDITLINE_INCLUDE_DIR AND EDITLINE_LIBRARY)
    message(STATUS "walletconsole: line editing enabled (${EDITLINE_LIBRARY})")
    target_compile_definitions(walletconsolelib PUBLIC WALLETCONSOLE_LINE_EDIT)
    target_include_directories(walletconsolelib PRIVATE ${EDITLINE_INCLUDE_DIR})
    target_link_libraries(walletconsolelib ${EDITLINE_LIBRARY})
else()
    message(STATUS "walletconsole: libedit not found, no tab completion")
endif()
