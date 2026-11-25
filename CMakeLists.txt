cmake_minimum_required(VERSION 3.16)

project(MsDevBox
    VERSION 0.1.0
    DESCRIPTION "Microsoft Dev Box client library for Linux: Entra login, workspace discovery, .rdp generation, Teams VDI optimization"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(MSDEVBOX_BUILD_CLI "Build the msdevbox command line tool" ON)
option(MSDEVBOX_BUILD_CHANNEL "Install the teamscap FreeRDP channel integration files" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS NetworkAuth)

add_library(MsDevBox SHARED
    src/auth.cpp
    src/discovery.cpp
    src/rdpfile.cpp
    src/tokenstore.cpp
    include/msdevbox/auth.h
    include/msdevbox/constants.h
    include/msdevbox/discovery.h
    include/msdevbox/rdpfile.h
    src/tokenstore.h
)
add_library(MsDevBox::MsDevBox ALIAS MsDevBox)
set_target_properties(MsDevBox PROPERTIES AUTOMOC ON)

target_compile_definitions(MsDevBox PRIVATE MSDEVBOX_LIBRARY)
target_include_directories(MsDevBox
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(MsDevBox PUBLIC Qt6::Core Qt6::Network)

set_target_properties(MsDevBox PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER "include/msdevbox/auth.h;include/msdevbox/discovery.h;include/msdevbox/rdpfile.h;include/msdevbox/constants.h"
)

if(MSDEVBOX_BUILD_CLI)
    add_executable(msdevbox tools/msdevbox-cli.cpp)
    target_link_libraries(msdevbox PRIVATE MsDevBox::MsDevBox)
endif()

include(GNUInstallDirs)
install(TARGETS MsDevBox
    EXPORT MsDevBoxTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/msdevbox
)
install(EXPORT MsDevBoxTargets
    NAMESPACE MsDevBox::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MsDevBox
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MsDevBoxConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/MsDevBoxConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MsDevBox
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/MsDevBoxConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/MsDevBoxConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/MsDevBoxConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/MsDevBox
)
