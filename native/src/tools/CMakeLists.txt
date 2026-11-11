# Offline checks of shader packs that extend radiante-pt, without the game: stages a pack the way the loader does
# (shader_pack_extends.cpp), with --self-test runs the merge rules' own checks, and with --extract unpacks a zip
# through the loader's checked extraction. Not part of the default build:
#   cmake --build <build dir> --config Release --target shader_pack_stage
add_executable(shader_pack_stage EXCLUDE_FROM_ALL
    shader_pack_stage.cpp
    ${PROJECT_SOURCE_DIR}/src/prism/render/modules/world/shader_pack/shader_pack_extends.cpp)
target_include_directories(shader_pack_stage PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(shader_pack_stage PRIVATE nlohmann_json::nlohmann_json minizip)

# Offline geometric conservativeness checks, independent of Minecraft or a Vulkan device.
add_executable(hand_primary_bounds_test EXCLUDE_FROM_ALL hand_primary_bounds_test.cpp)
target_include_directories(hand_primary_bounds_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${GLM_INCLUDE_DIR})

add_executable(held_light_history_test EXCLUDE_FROM_ALL held_light_history_test.cpp)
target_include_directories(held_light_history_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${GLM_INCLUDE_DIR})

# The emission checks compile the renderer's emission code into themselves (it needs no device), so they take the
# renderer's include paths and definitions but link none of it.
foreach(emission_test emission_multiplier_test held_item_emission_test)
    add_executable(${emission_test} EXCLUDE_FROM_ALL ${emission_test}.cpp
        ${PROJECT_SOURCE_DIR}/src/prism/render/emission.cpp)
    target_include_directories(${emission_test} PRIVATE ${PROJECT_SOURCE_DIR}/src
        $<TARGET_PROPERTY:prismrt,INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(${emission_test} PRIVATE $<TARGET_PROPERTY:prismrt,INTERFACE_COMPILE_DEFINITIONS>)
endforeach()

# The offline checks as tests: build them with the native_tests target, then run ctest in the build folder
# (ctest -C Release --output-on-failure). The CI does both.
set(PRISM_NATIVE_TESTS hand_primary_bounds_test held_light_history_test emission_multiplier_test
    held_item_emission_test)
add_custom_target(native_tests DEPENDS shader_pack_stage ${PRISM_NATIVE_TESTS})
foreach(native_test ${PRISM_NATIVE_TESTS})
    add_test(NAME ${native_test} COMMAND ${native_test})
endforeach()
add_test(NAME shader_pack_stage_self_test COMMAND shader_pack_stage --self-test)
