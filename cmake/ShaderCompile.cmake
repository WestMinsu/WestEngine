# =============================================================================
# WestEngine - Shader Compile
# Slang offline compilation rules for DXIL/SPIR-V and reflection metadata
# =============================================================================

set(_west_slang_tool_paths)

foreach(_west_slang_triplet IN ITEMS "${VCPKG_HOST_TRIPLET}" "${VCPKG_TARGET_TRIPLET}")
    if(_west_slang_triplet AND DEFINED VCPKG_INSTALLED_DIR)
        list(APPEND _west_slang_tool_paths
            "${VCPKG_INSTALLED_DIR}/${_west_slang_triplet}/tools/shader-slang")
    endif()
endforeach()

list(REMOVE_DUPLICATES _west_slang_tool_paths)

unset(WEST_SLANGC_EXECUTABLE CACHE)
find_program(WEST_SLANGC_EXECUTABLE
    NAMES slangc slangc.exe
    PATHS ${_west_slang_tool_paths}
    NO_DEFAULT_PATH
)

if(NOT WEST_SLANGC_EXECUTABLE)
    message(FATAL_ERROR
        "slangc was not found in vcpkg tools/shader-slang. "
        "Install the manifest dependency 'shader-slang' through vcpkg.")
endif()

unset(_west_slang_tool_paths)
unset(_west_slang_triplet)

set(WEST_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders" CACHE INTERNAL "WestEngine compiled shader output dir")
set(WEST_SHADER_GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated" CACHE INTERNAL "WestEngine generated header dir")
set(WEST_SHADER_METADATA_HEADER "${WEST_SHADER_GENERATED_DIR}/ShaderMetadata.h"
    CACHE INTERNAL "WestEngine generated shader metadata header")

file(MAKE_DIRECTORY "${WEST_SHADER_OUTPUT_DIR}")
file(MAKE_DIRECTORY "${WEST_SHADER_GENERATED_DIR}")

set_property(GLOBAL PROPERTY WEST_SHADER_REFLECTION_JSONS "")
set_property(GLOBAL PROPERTY WEST_SHADER_SOURCES "")
set_property(GLOBAL PROPERTY WEST_SHADER_OUTPUTS "")

function(west_add_slang_shader)
    cmake_parse_arguments(ARG
        ""
        "NAME;SOURCE;VERTEX;FRAGMENT;COMPUTE;VERTEX_PROFILE;FRAGMENT_PROFILE;COMPUTE_PROFILE"
        ""
        ${ARGN}
    )

    if(NOT ARG_NAME)
        message(FATAL_ERROR "west_add_slang_shader requires NAME")
    endif()
    if(NOT ARG_SOURCE)
        message(FATAL_ERROR "west_add_slang_shader(${ARG_NAME}) requires SOURCE")
    endif()

    set(_source "${ARG_SOURCE}")
    if(NOT IS_ABSOLUTE "${_source}")
        set(_source "${CMAKE_SOURCE_DIR}/${_source}")
    endif()

    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "Shader source does not exist: ${_source}")
    endif()

    get_property(_all_shader_sources GLOBAL PROPERTY WEST_SHADER_SOURCES)
    list(APPEND _all_shader_sources "${_source}")
    list(REMOVE_DUPLICATES _all_shader_sources)
    set_property(GLOBAL PROPERTY WEST_SHADER_SOURCES "${_all_shader_sources}")

    set(_shader_outputs "")
    set(_reflection_outputs "")
    set(_vertex_profile "vs_6_6")
    set(_fragment_profile "ps_6_6")
    set(_compute_profile "cs_6_6")

    if(ARG_VERTEX_PROFILE)
        set(_vertex_profile "${ARG_VERTEX_PROFILE}")
    endif()
    if(ARG_FRAGMENT_PROFILE)
        set(_fragment_profile "${ARG_FRAGMENT_PROFILE}")
    endif()
    if(ARG_COMPUTE_PROFILE)
        set(_compute_profile "${ARG_COMPUTE_PROFILE}")
    endif()

    function(_west_add_slang_entry _stage_suffix _entry _dxil_profile)
        set(_dxil_output "${WEST_SHADER_OUTPUT_DIR}/${ARG_NAME}.${_stage_suffix}.dxil")
        set(_spirv_output "${WEST_SHADER_OUTPUT_DIR}/${ARG_NAME}.${_stage_suffix}.spv")
        set(_dxil_reflection "${WEST_SHADER_OUTPUT_DIR}/${ARG_NAME}.${_stage_suffix}.dxil.reflection.json")
        set(_spirv_reflection "${WEST_SHADER_OUTPUT_DIR}/${ARG_NAME}.${_stage_suffix}.spv.reflection.json")
        set(_dxil_depfile "${WEST_SHADER_OUTPUT_DIR}/${ARG_NAME}.${_stage_suffix}.dxil.d")
        set(_spirv_depfile "${WEST_SHADER_OUTPUT_DIR}/${ARG_NAME}.${_stage_suffix}.spv.d")

        add_custom_command(
            OUTPUT "${_dxil_output}"
            BYPRODUCTS "${_dxil_reflection}" "${_dxil_depfile}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEST_SHADER_OUTPUT_DIR}"
            COMMAND "${WEST_SLANGC_EXECUTABLE}" "${_source}"
                -I "${CMAKE_SOURCE_DIR}/shaders"
                -entry "${_entry}"
                -target dxil
                -profile "${_dxil_profile}"
                -g
                -o "${_dxil_output}"
                -reflection-json "${_dxil_reflection}"
                -depfile "${_dxil_depfile}"
            DEPFILE "${_dxil_depfile}"
            DEPENDS "${_source}"
            COMMENT "Compiling ${ARG_NAME}.${_stage_suffix}.dxil"
            VERBATIM
        )

        add_custom_command(
            OUTPUT "${_spirv_output}"
            BYPRODUCTS "${_spirv_reflection}" "${_spirv_depfile}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEST_SHADER_OUTPUT_DIR}"
            COMMAND "${WEST_SLANGC_EXECUTABLE}" "${_source}"
                -I "${CMAKE_SOURCE_DIR}/shaders"
                -entry "${_entry}"
                -target spirv
                -profile spirv_1_5
                -g
                -o "${_spirv_output}"
                -reflection-json "${_spirv_reflection}"
                -depfile "${_spirv_depfile}"
            DEPFILE "${_spirv_depfile}"
            DEPENDS "${_source}"
            COMMENT "Compiling ${ARG_NAME}.${_stage_suffix}.spv"
            VERBATIM
        )

        list(APPEND _shader_outputs "${_dxil_output}" "${_spirv_output}")
        list(APPEND _reflection_outputs "${_dxil_reflection}" "${_spirv_reflection}")
        set(_shader_outputs "${_shader_outputs}" PARENT_SCOPE)
        set(_reflection_outputs "${_reflection_outputs}" PARENT_SCOPE)
    endfunction()

    if(ARG_VERTEX)
        _west_add_slang_entry("vs" "${ARG_VERTEX}" "${_vertex_profile}")
    endif()
    if(ARG_FRAGMENT)
        _west_add_slang_entry("ps" "${ARG_FRAGMENT}" "${_fragment_profile}")
    endif()
    if(ARG_COMPUTE)
        _west_add_slang_entry("cs" "${ARG_COMPUTE}" "${_compute_profile}")
    endif()

    if(NOT _shader_outputs)
        message(FATAL_ERROR "west_add_slang_shader(${ARG_NAME}) requires at least one entry point")
    endif()

    get_property(_all_reflections GLOBAL PROPERTY WEST_SHADER_REFLECTION_JSONS)
    list(APPEND _all_reflections ${_reflection_outputs})
    set_property(GLOBAL PROPERTY WEST_SHADER_REFLECTION_JSONS "${_all_reflections}")

    get_property(_all_shader_outputs GLOBAL PROPERTY WEST_SHADER_OUTPUTS)
    list(APPEND _all_shader_outputs ${_shader_outputs})
    set_property(GLOBAL PROPERTY WEST_SHADER_OUTPUTS "${_all_shader_outputs}")
endfunction()

function(west_finalize_slang_shaders)
    get_property(_all_reflections GLOBAL PROPERTY WEST_SHADER_REFLECTION_JSONS)
    get_property(_all_shader_sources GLOBAL PROPERTY WEST_SHADER_SOURCES)
    get_property(_all_shader_outputs GLOBAL PROPERTY WEST_SHADER_OUTPUTS)

    if(NOT _all_reflections OR NOT _all_shader_outputs)
        message(FATAL_ERROR "west_finalize_slang_shaders called before any Slang shaders were registered")
    endif()

    add_custom_command(
        OUTPUT "${WEST_SHADER_METADATA_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${WEST_SHADER_GENERATED_DIR}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/extract_metadata.py"
            --output "${WEST_SHADER_METADATA_HEADER}"
            ${_all_reflections}
        DEPENDS "${CMAKE_SOURCE_DIR}/tools/extract_metadata.py" ${_all_shader_outputs} ${_all_reflections}
        COMMENT "Generating ShaderMetadata.h"
        VERBATIM
    )

    set_source_files_properties(${_all_shader_outputs} "${WEST_SHADER_METADATA_HEADER}"
        PROPERTIES
            GENERATED TRUE
    )

    set(WEST_SHADER_BUILD_OUTPUTS ${_all_shader_outputs} "${WEST_SHADER_METADATA_HEADER}"
        CACHE INTERNAL "WestEngine generated shader files")
    set(WEST_SHADER_SOURCES ${_all_shader_sources}
        CACHE INTERNAL "WestEngine Slang shader source files")
endfunction()
