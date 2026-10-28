# Offline checks of shader packs that extend radiante-pt, without the game: stages a pack the way the loader does
# (shader_pack_extends.cpp), with --self-test runs the merge rules' own checks, and with --extract unpacks a zip
# through the loader's checked extraction. Not part of the default build:
#   cmake --build <build dir> --config Release --target shader_pack_stage
add_executable(shader_pack_stage EXCLUDE_FROM_ALL
    shader_pack_stage.cpp
    ${PROJECT_SOURCE_DIR}/src/prism/render/modules/world/shader_pack/shader_pack_extends.cpp)
target_include_directories(shader_pack_stage PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(shader_pack_stage PRIVATE nlohmann_json::nlohmann_json minizip)
