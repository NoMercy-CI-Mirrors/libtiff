# Library reference documentation. Use CMake's standard FindDoxygen module.
# The full reference is slow to generate and large to install, so it is only
# built when explicitly requested.
option(doxygen "Build Doxygen library reference documentation" OFF)
option(doxygen-checks
       "Fail the build on Doxygen warnings or incomplete documentation markup"
       OFF)

if(NOT doxygen)
  if(doxygen-checks)
    message(FATAL_ERROR "-Ddoxygen-checks=ON requires -Ddoxygen=ON")
  endif()
  message(STATUS "Doxygen library reference: disabled")
  return()
endif()

# An explicit request must not silently succeed without the required tool.
find_package(Doxygen 1.9.1 REQUIRED COMPONENTS doxygen OPTIONAL_COMPONENTS dot)
# CMAKE_DISABLE_FIND_PACKAGE_Doxygen diagnoses REQUIRED without loading the
# module. Avoid secondary errors from undefined commands in that case.
if(NOT Doxygen_FOUND)
  return()
endif()

set(DOXYGEN_PROJECT_NAME "LibTIFF")
set(DOXYGEN_PROJECT_NUMBER "${LIBTIFF_VERSION_FULL}")
set(DOXYGEN_PROJECT_BRIEF "TIFF library public API and implementation reference")
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(DOXYGEN_HTML_OUTPUT api)
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_GENERATE_LATEX NO)
set(DOXYGEN_GENERATE_MAN NO)
set(DOXYGEN_GENERATE_TAGFILE "${CMAKE_CURRENT_BINARY_DIR}/libtiff.tag")
set(DOXYGEN_USE_MDFILE_AS_MAINPAGE "${CMAKE_CURRENT_SOURCE_DIR}/index.md")
set(DOXYGEN_STRIP_FROM_PATH "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}")
set(DOXYGEN_FULL_PATH_NAMES YES)
set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
set(DOXYGEN_JAVADOC_AUTOBRIEF NO)
set(DOXYGEN_EXTRACT_ALL NO)
set(DOXYGEN_EXTRACT_PRIVATE YES)
set(DOXYGEN_EXTRACT_STATIC YES)
set(DOXYGEN_INTERNAL_DOCS YES)
set(DOXYGEN_SOURCE_BROWSER YES)
set(DOXYGEN_REFERENCED_BY_RELATION YES)
set(DOXYGEN_REFERENCES_RELATION YES)
set(DOXYGEN_QUIET YES)
set(DOXYGEN_WARNINGS YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_WARN_IF_DOC_ERROR YES)
set(DOXYGEN_WARN_NO_PARAMDOC YES)
# Record documentation warnings in the log. Doxygen itself does not fail on
# them because graph size limits are also reported as warnings; the
# doxygen-checks target below fails on every other warning instead.
set(DOXYGEN_WARN_AS_ERROR NO)
set(DOXYGEN_WARN_LOGFILE "${CMAKE_CURRENT_BINARY_DIR}/warnings.log")

set(DOXYGEN_ENABLE_PREPROCESSING YES)
set(DOXYGEN_MACRO_EXPANSION YES)
set(DOXYGEN_EXPAND_ONLY_PREDEF YES)
# Honour header-defined visibility and configured printf-format selection.
# Without expansion, these #if expressions can hide documented definitions.
set(DOXYGEN_EXPAND_AS_DEFINED LOGLUV_PUBLIC SIZEOF_SIZE_T SIZEOF_WORDTYPE
    DECLAREContigPutFunc DECLARESepPutFunc EMSG_BUF_SIZE)
set(DOXYGEN_PREDEFINED "TIFF_ATTRIBUTE(x)=" "TIFF_GCC_DEPRECATED="
    "TIFF_MSC_DEPRECATED="
    # Include optional JBIG source contracts even without the dependency.
    "JBIG_SUPPORT=1"
    # Expose the IJG 9d compatibility helper independently of host headers.
    "JPEG_LIB_VERSION_MAJOR=9" "JPEG_LIB_VERSION_MINOR=4")
set(DOXYGEN_SEARCH_INCLUDES YES)
set(DOXYGEN_INCLUDE_PATH "${PROJECT_SOURCE_DIR}/libtiff"
    "${PROJECT_BINARY_DIR}/libtiff")

# FindDoxygen controls HAVE_DOT according to the optional dot component.
set(DOXYGEN_DOT_IMAGE_FORMAT svg)
set(DOXYGEN_INTERACTIVE_SVG YES)
set(DOXYGEN_CLASS_GRAPH YES)
set(DOXYGEN_COLLABORATION_GRAPH YES)
set(DOXYGEN_GROUP_GRAPHS YES)
set(DOXYGEN_INCLUDE_GRAPH YES)
set(DOXYGEN_INCLUDED_BY_GRAPH YES)
set(DOXYGEN_DIRECTORY_GRAPH YES)
set(DOXYGEN_CALL_GRAPH YES)
set(DOXYGEN_CALLER_GRAPH YES)

# Scan the library only, including optional codecs and both platform backends.
# CONFIGURE_DEPENDS also picks up newly added library and documentation files.
file(GLOB doxygen_library_inputs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/libtiff/*.h"
    "${PROJECT_SOURCE_DIR}/libtiff/*.hxx"
    "${PROJECT_SOURCE_DIR}/libtiff/*.c"
    "${PROJECT_SOURCE_DIR}/libtiff/*.cxx")
file(GLOB doxygen_pages CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.dox")
# Exclude the standalone table generator and any headers left by a different
# in-source build. The configured headers for this build are appended below.
list(REMOVE_ITEM doxygen_library_inputs
    "${PROJECT_SOURCE_DIR}/libtiff/mkspans.c"
    "${PROJECT_SOURCE_DIR}/libtiff/tiffconf.h"
    "${PROJECT_SOURCE_DIR}/libtiff/tiffvers.h"
    "${PROJECT_SOURCE_DIR}/libtiff/tif_config.h")
list(APPEND doxygen_library_inputs
    "${PROJECT_BINARY_DIR}/libtiff/tiffconf.h"
    "${PROJECT_BINARY_DIR}/libtiff/tiffvers.h"
    "${PROJECT_BINARY_DIR}/libtiff/tif_config.h")
list(REMOVE_DUPLICATES doxygen_library_inputs)

if(doxygen-checks)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  # The structural checks read the XML output.
  set(DOXYGEN_GENERATE_XML YES)
endif()

doxygen_add_docs(tiff-doc-doxygen
    ${doxygen_library_inputs} ${doxygen_pages}
    ALL
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Generating LibTIFF API and implementation reference")

if(doxygen-checks)
  # Run after every reference build, as part of the default build, so that
  # missing documentation, invalid markup and incomplete contracts fail it.
  add_custom_target(tiff-doc-doxygen-check ALL
      COMMAND "${Python3_EXECUTABLE}" -B
              "${CMAKE_CURRENT_SOURCE_DIR}/check-xml.py"
              --warnings-log "${DOXYGEN_WARN_LOGFILE}"
              "${CMAKE_CURRENT_BINARY_DIR}/xml"
      COMMENT "Checking LibTIFF Doxygen warnings and documentation markup"
      VERBATIM)
  add_dependencies(tiff-doc-doxygen-check tiff-doc-doxygen)
endif()

if(tiff-install)
  install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/api"
          DESTINATION "${LIBTIFF_DOCDIR}"
          COMPONENT documentation)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libtiff.tag"
          DESTINATION "${LIBTIFF_DOCDIR}"
          COMPONENT documentation)
endif()

message(STATUS "Doxygen library reference: enabled (target tiff-doc-doxygen)")
if(doxygen-checks)
  message(STATUS "Doxygen markup checks: enabled (target tiff-doc-doxygen-check)")
endif()
if(TARGET Doxygen::dot)
  message(STATUS "Doxygen graphs: linked, interactive SVG (Graphviz)")
else()
  message(STATUS "Doxygen graphs: disabled (Graphviz dot not found)")
endif()
