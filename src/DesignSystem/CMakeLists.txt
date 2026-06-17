cmake_minimum_required(VERSION 3.22)

add_library(DesignSystem STATIC
    # Core
    src/Core/TokenValue.cpp
    src/Core/Context.cpp

    # Tokens
    src/Tokens/Token.cpp
    src/Tokens/TokenSchema.cpp
    src/Tokens/TokenRegistry.cpp
    src/Tokens/ThemeDefinition.cpp

    # Override
    src/Override/Override.cpp
    src/Override/OverrideManager.cpp

    # Accessibility
    src/Accessibility/ColorBlindness.cpp

    # Persistence
    src/Persistence/Serialization.cpp

    # Facade
    src/DesignSystem.cpp
)

# Public headers (exported to consumers)
target_include_directories(DesignSystem
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)
