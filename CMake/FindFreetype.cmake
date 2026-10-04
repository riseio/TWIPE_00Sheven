set(FREETYPE_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/include")
set(FREETYPE_LIBRARIES
    "${CMAKE_SOURCE_DIR}/lib/RecompFrontend/recompui/lib/freetype-windows-binaries/release dll/win64/freetype.lib")

add_library(Freetype::Freetype SHARED IMPORTED)
set_target_properties(Freetype::Freetype PROPERTIES
    IMPORTED_IMPLIB "${FREETYPE_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIRS}")
