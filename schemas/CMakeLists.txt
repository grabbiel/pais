# schemas/CMakeLists.txt
# ============================================================================
# Pixel Life - Schema Generation (FlatBuffers)
# ============================================================================

# Only process if FlatBuffers is available
if(TARGET flatbuffers)
  set(SCHEMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/flatbuffers)
  set(SCHEMA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

  # Find schema files
  file(GLOB SCHEMA_FILES ${SCHEMA_DIR}/*.fbs)

  if(SCHEMA_FILES)
    # Create output directory
    file(MAKE_DIRECTORY ${SCHEMA_GEN_DIR})

    # Determine flatc executable
    if(TARGET flatbuffers::flatc)
      set(FLATC_EXECUTABLE $<TARGET_FILE:flatbuffers::flatc>)
      set(FLATC_TARGET flatbuffers::flatc)
    elseif(TARGET flatc)
      set(FLATC_EXECUTABLE $<TARGET_FILE:flatc>)
      set(FLATC_TARGET flatc)
    else()
      message(WARNING "FlatBuffers flatc target not found, skipping schema generation")
      return()
    endif()

    # Generate C++ headers from schemas
    set(SCHEMA_OUTPUTS "")
    foreach(SCHEMA_FILE ${SCHEMA_FILES})
      get_filename_component(SCHEMA_NAME ${SCHEMA_FILE} NAME_WE)
      set(SCHEMA_OUTPUT ${SCHEMA_GEN_DIR}/${SCHEMA_NAME}_generated.h)

      add_custom_command(
        OUTPUT ${SCHEMA_OUTPUT}
        COMMAND ${FLATC_EXECUTABLE}
          --cpp
          -o ${SCHEMA_GEN_DIR}
          ${SCHEMA_FILE}
        DEPENDS ${SCHEMA_FILE} ${FLATC_TARGET}
        COMMENT "Generating FlatBuffer header: ${SCHEMA_NAME}_generated.h"
        VERBATIM
      )

      list(APPEND SCHEMA_OUTPUTS ${SCHEMA_OUTPUT})
    endforeach()

    # Create interface library for generated headers
    add_library(pixel_schemas INTERFACE)
    target_include_directories(pixel_schemas INTERFACE
      ${SCHEMA_GEN_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(pixel_schemas INTERFACE flatbuffers)

    # Create custom target to trigger generation
    add_custom_target(generate_schemas ALL DEPENDS ${SCHEMA_OUTPUTS})
    add_dependencies(pixel_schemas generate_schemas)

    # Create alias
    if(NOT TARGET pixel::schemas)
      add_library(pixel::schemas ALIAS pixel_schemas)
    endif()

    message(STATUS "Schemas: FlatBuffers code generation enabled")
  else()
    message(STATUS "Schemas: No .fbs files found in ${SCHEMA_DIR}")
  endif()
else()
  message(STATUS "Schemas: FlatBuffers not available, skipping")
endif()
