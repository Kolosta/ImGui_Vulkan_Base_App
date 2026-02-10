# # # cmake_minimum_required(VERSION 3.22)
# # # project(VulkanImGuiProject)

# # # set(CMAKE_CXX_STANDARD 20)
# # # set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # # # Vulkan
# # # find_package(Vulkan REQUIRED)

# # # include(FetchContent)

# # # # SDL3
# # # FetchContent_Declare(
# # #     SDL3
# # #     GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
# # #     GIT_TAG main
# # # )
# # # FetchContent_MakeAvailable(SDL3)

# # # # ImGui
# # # FetchContent_Declare(
# # #     imgui
# # #     GIT_REPOSITORY https://github.com/ocornut/imgui.git
# # #     GIT_TAG docking
# # # )
# # # FetchContent_MakeAvailable(imgui)

# # # FetchContent_Declare(
# # #     nanosvg
# # #     GIT_REPOSITORY https://github.com/memononen/nanosvg.git
# # #     GIT_TAG master
# # # )
# # # FetchContent_MakeAvailable(nanosvg)

# # # file(WRITE
# # #     ${CMAKE_BINARY_DIR}/nanosvg_impl.cpp
# # #     "#define NANOSVG_IMPLEMENTATION\n"
# # #     "#define NANOSVGRAST_IMPLEMENTATION\n"
# # #     "#include <nanosvg.h>\n"
# # #     "#include <nanosvgrast.h>\n"
# # # )

# # # # add_library(nanosvg STATIC
# # # #     ${CMAKE_BINARY_DIR}/nanosvg_impl.cpp
# # # # )

# # # # ImGui sources
# # # set(IMGUI_DIR ${imgui_SOURCE_DIR})
# # # set(IMGUI_SOURCES
# # #     ${IMGUI_DIR}/imgui.cpp
# # #     ${IMGUI_DIR}/imgui_demo.cpp
# # #     ${IMGUI_DIR}/imgui_draw.cpp
# # #     ${IMGUI_DIR}/imgui_tables.cpp
# # #     ${IMGUI_DIR}/imgui_widgets.cpp
# # #     ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
# # #     ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
# # # )

# # # # DesignSystem
# # # add_subdirectory(src/DesignSystem)

# # # # Executable
# # # add_executable(${PROJECT_NAME}
# # #     src/main.cpp
# # #     src/ShortcutManager.cpp
# # #     src/ShortcutEditor.cpp
# # #     src/DesignSystem/src/UI/SvgManager.cpp
# # #     ${IMGUI_SOURCES}
# # #     ${CMAKE_BINARY_DIR}/nanosvg_impl.cpp
# # # )

# # # target_compile_definitions(
# # #     ${PROJECT_NAME} PRIVATE IMGUI_ENABLE_DOCKING
# # # )


# # # target_include_directories(${PROJECT_NAME} PRIVATE
# # #     ${IMGUI_DIR}
# # #     ${IMGUI_DIR}/backends
# # #     ${CMAKE_CURRENT_SOURCE_DIR}/src
# # #     ${nanosvg_SOURCE_DIR}/src
# # # )

# # # target_link_libraries(${PROJECT_NAME} PRIVATE
# # #     Vulkan::Vulkan
# # #     SDL3::SDL3-shared
# # #     DesignSystem
# # #     # nanosvg
# # # )

# # # # Copy resources if needed
# # # file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})







# # cmake_minimum_required(VERSION 3.22)
# # project(Carto)

# # set(CMAKE_CXX_STANDARD 20)
# # set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # # Vulkan
# # find_package(Vulkan REQUIRED)

# # include(FetchContent)

# # # SDL3
# # FetchContent_Declare(
# #     SDL3
# #     GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
# #     GIT_TAG main
# # )
# # FetchContent_MakeAvailable(SDL3)

# # # ImGui
# # FetchContent_Declare(
# #     imgui
# #     GIT_REPOSITORY https://github.com/ocornut/imgui.git
# #     GIT_TAG docking
# # )
# # FetchContent_MakeAvailable(imgui)

# # # NanoSVG
# # FetchContent_Declare(
# #     nanosvg
# #     GIT_REPOSITORY https://github.com/memononen/nanosvg.git
# #     GIT_TAG master
# # )
# # FetchContent_MakeAvailable(nanosvg)

# # # NanoSVG implementation
# # file(WRITE
# #     ${CMAKE_BINARY_DIR}/nanosvg_impl.cpp
# #     "#define NANOSVG_IMPLEMENTATION\n"
# #     "#define NANOSVGRAST_IMPLEMENTATION\n"
# #     "#include <nanosvg.h>\n"
# #     "#include <nanosvgrast.h>\n"
# # )

# # # ImGui sources
# # set(IMGUI_DIR ${imgui_SOURCE_DIR})
# # set(IMGUI_SOURCES
# #     ${IMGUI_DIR}/imgui.cpp
# #     ${IMGUI_DIR}/imgui_demo.cpp
# #     ${IMGUI_DIR}/imgui_draw.cpp
# #     ${IMGUI_DIR}/imgui_tables.cpp
# #     ${IMGUI_DIR}/imgui_widgets.cpp
# #     ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
# #     ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
# # )

# # # Subdirectories
# # add_subdirectory(src/DesignSystem)
# # add_subdirectory(src/UI)
# # add_subdirectory(src/Shortcuts)
# # add_subdirectory(src/Application)

# # # Main executable
# # add_executable(${PROJECT_NAME}
# #     src/main.cpp
# #     ${IMGUI_SOURCES}
# #     ${CMAKE_BINARY_DIR}/nanosvg_impl.cpp
# # )

# # target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_ENABLE_DOCKING)

# # target_include_directories(${PROJECT_NAME} PRIVATE
# #     ${IMGUI_DIR}
# #     ${IMGUI_DIR}/backends
# #     ${CMAKE_CURRENT_SOURCE_DIR}/src
# #     ${nanosvg_SOURCE_DIR}/src
# # )

# # target_link_libraries(${PROJECT_NAME} PRIVATE
# #     Vulkan::Vulkan
# #     SDL3::SDL3-shared
# #     DesignSystem
# #     UI
# #     Shortcuts
# #     Application
# # )

# # # Copy resources
# # file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})





# cmake_minimum_required(VERSION 3.22)
# project(Carto)

# set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # Vulkan
# find_package(Vulkan REQUIRED)

# include(FetchContent)

# # SDL3
# FetchContent_Declare(
#     SDL3
#     GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
#     GIT_TAG main
# )
# FetchContent_MakeAvailable(SDL3)

# # ImGui
# FetchContent_Declare(
#     imgui
#     GIT_REPOSITORY https://github.com/ocornut/imgui.git
#     GIT_TAG docking
# )
# FetchContent_MakeAvailable(imgui)

# # ImGui sources
# set(IMGUI_DIR ${imgui_SOURCE_DIR})
# set(IMGUI_SOURCES
#     ${IMGUI_DIR}/imgui.cpp
#     ${IMGUI_DIR}/imgui_demo.cpp
#     ${IMGUI_DIR}/imgui_draw.cpp
#     ${IMGUI_DIR}/imgui_tables.cpp
#     ${IMGUI_DIR}/imgui_widgets.cpp
#     ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
#     ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
# )

# # Subdirectories
# add_subdirectory(src/DesignSystem)
# add_subdirectory(src/UI)
# add_subdirectory(src/Shortcuts)
# add_subdirectory(src/VectorGraphics)
# add_subdirectory(src/Application)

# # Main executable
# add_executable(${PROJECT_NAME}
#     src/main.cpp
#     ${IMGUI_SOURCES}
# )

# target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_ENABLE_DOCKING)

# target_include_directories(${PROJECT_NAME} PRIVATE
#     ${IMGUI_DIR}
#     ${IMGUI_DIR}/backends
#     ${CMAKE_CURRENT_SOURCE_DIR}/src
# )

# target_link_libraries(${PROJECT_NAME} PRIVATE
#     Vulkan::Vulkan
#     SDL3::SDL3-shared
#     DesignSystem
#     UI
#     Shortcuts
#     VectorGraphics
#     Application
# )

# # Copy resources
# file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})





cmake_minimum_required(VERSION 3.22)
project(Carto)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure Cargo is available
find_program(CARGO_EXECUTABLE cargo REQUIRED)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "Cargo not found. Please install Rust and Cargo.")
endif()

# Vulkan
find_package(Vulkan REQUIRED)

include(FetchContent)

# SDL3
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.0
)
FetchContent_MakeAvailable(SDL3)

# ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
)
FetchContent_MakeAvailable(imgui)

# ImGui sources
set(IMGUI_DIR ${imgui_SOURCE_DIR})
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Subdirectories
add_subdirectory(src/DesignSystem)
add_subdirectory(src/UI)
add_subdirectory(src/Shortcuts)
add_subdirectory(src/VectorGraphics)
add_subdirectory(src/Application)

# Main executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${IMGUI_SOURCES}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_ENABLE_DOCKING)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    SDL3::SDL3-shared
    DesignSystem
    UI
    Shortcuts
    VectorGraphics
    Application
)

# Copy resources
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


if(WIN32 AND MINGW)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE_DIR:VectorGraphics>/resvg_c.dll
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/resvg_c.dll
        COMMENT "Copying resvg DLL to executable directory"
    )

    # Récupère le répertoire du compilateur
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    # Sous la section où tu copies déjà les DLL MinGW
    if(TARGET SDL3::SDL3-shared)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL3::SDL3-shared>"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
            COMMENT "Copying SDL3 DLL to executable directory"
        )
    endif()
endif()
