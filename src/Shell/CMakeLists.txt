cmake_minimum_required(VERSION 3.22)

# ── Shell: Windows shell integration for the .acu format ──────────────────────
# Two artifacts (Windows only):
#   • Shell (static lib) — per-user registry registration, linked into the app.
#   • acu_thumbs (DLL)   — the IThumbnailProvider COM server Explorer loads to
#                          render .acu previews from the THMB section.
# On non-Windows the static lib still builds (its functions are no-ops) and the
# DLL is skipped.

add_library(Shell STATIC
    src/ShellIntegration.cpp
)
target_include_directories(Shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(Shell PUBLIC shlwapi)
endif()

if(WIN32)
    add_library(acu_thumbs SHARED
        thumbprovider/AcuThumbProvider.cpp
        thumbprovider/acu_thumbs.def
    )
    # stb_image.h (header-only PNG decoder) lives in third_party/stb.
    target_include_directories(acu_thumbs PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/stb)
    # shlwapi (Path*), uuid/ole32 (IID_*/CLSID + COM), propsys (shell prop IIDs).
    target_link_libraries(acu_thumbs PRIVATE shlwapi uuid ole32 propsys)
    # Statically link libgcc/libstdc++ so Explorer can load the DLL without the
    # MinGW runtime DLLs being on its PATH.
    if(MINGW)
        target_link_options(acu_thumbs PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    # MinGW prefixes shared libs with "lib" by default; the registry points at a
    # bare "acu_thumbs.dll", so strip the prefix (and pin the name).
    set_target_properties(acu_thumbs PROPERTIES OUTPUT_NAME "acu_thumbs" PREFIX "")
endif()
