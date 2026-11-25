# MsDevBoxConfig.cmake — package config for the Rust-built Dev Box client
# library. Installed by scripts/install.sh; consumed with:
#     find_package(MsDevBox 0.1 REQUIRED CONFIG)
#     target_link_libraries(myclient PRIVATE MsDevBox::MsDevBox)

find_library(MSDEVBOX_LIBRARY NAMES msdevbox REQUIRED)
find_path(MSDEVBOX_INCLUDE_DIR msdevbox.h REQUIRED)

if(NOT TARGET MsDevBox::MsDevBox)
    add_library(MsDevBox::MsDevBox UNKNOWN IMPORTED)
    set_target_properties(MsDevBox::MsDevBox PROPERTIES
        IMPORTED_LOCATION "${MSDEVBOX_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MSDEVBOX_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(MSDEVBOX_LIBRARY MSDEVBOX_INCLUDE_DIR)
