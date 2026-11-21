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

add_executable(exr_writer_test EXCLUDE_FROM_ALL exr_writer_test.cpp
    ${PROJECT_SOURCE_DIR}/src/prism/render/exr_writer.cpp)
target_include_directories(exr_writer_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${GLM_INCLUDE_DIR} ${STB_INCLUDE_DIR})

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
set(PRISM_NATIVE_TESTS hand_primary_bounds_test held_light_history_test exr_writer_test emission_multiplier_test
    held_item_emission_test)
add_custom_target(native_tests DEPENDS shader_pack_stage ${PRISM_NATIVE_TESTS})
foreach(native_test ${PRISM_NATIVE_TESTS})
    add_test(NAME ${native_test} COMMAND ${native_test})
endforeach()
add_test(NAME shader_pack_stage_self_test COMMAND shader_pack_stage --self-test)

# The SPIR-V bundle (radiante-pt-spirv.zip, next to radiante-pt.zip): radiante-pt's shaders at its default settings,
# compiled when Radiante is built so that a first start, and the first start after an update, compiles nothing
# (gpu::Shader::setBundle). The tool loads the renderer library just built and compiles with its own code
# (prismrtCompileSpirvBundle), so the files are named and made as the game's cache would make them. Off by default:
# local builds are for trying changes, where the shaders change on every build; the CI's release builds turn it on.
option(PRISM_SPIRV_BUNDLE "Compile radiante-pt's SPIR-V bundle at install" OFF)
set(PRISM_SPIRV_BUNDLE_ZIP "${PRISM_INSTALL_LIB_DIR}/shaders/world/ray_tracing/radiante-pt-spirv.zip")
if(PRISM_SPIRV_BUNDLE)
    add_executable(spirv_bundle spirv_bundle.cpp)
    target_include_directories(spirv_bundle PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(spirv_bundle PRIVATE minizip)
    if(NOT WIN32)
        target_link_libraries(spirv_bundle PRIVATE ${CMAKE_DL_LIBS})
    endif()
    add_dependencies(spirv_bundle prismrt)
    install(CODE "
        execute_process(
            COMMAND \"$<TARGET_FILE:spirv_bundle>\" \"$<TARGET_FILE:prismrt>\" \"${PRISM_INSTALL_LIB_DIR}/shaders\"
                    \"${PROJECT_SOURCE_DIR}/src/shader/spirv_bundle_variants.json\"
                    \"${CMAKE_BINARY_DIR}/spirv_bundle_work\" \"${PRISM_SPIRV_BUNDLE_ZIP}\"
            RESULT_VARIABLE BUNDLE_RESULT)
        if(NOT BUNDLE_RESULT EQUAL 0)
            message(FATAL_ERROR \"spirv_bundle failed (\${BUNDLE_RESULT})\")
        endif()
    ")
else()
    # Without it no bundle is installed: one left from an earlier build would only miss (its names no longer match
    # changed shaders), but would go into a local jar for nothing.
    install(CODE "file(REMOVE \"${PRISM_SPIRV_BUNDLE_ZIP}\")")
endif()
