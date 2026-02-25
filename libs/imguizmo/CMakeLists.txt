set(IMGUI_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../imgui)

file(GLOB IMGUIzmo_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_library(ImGuizmo STATIC ${IMGUIzmo_SOURCES})

target_include_directories(ImGuizmo PUBLIC
        ${IMGUI_PATH}
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(ImGuizmo PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(ImGuizmo PUBLIC imgui)