cmake_minimum_required(VERSION 3.22)
project(realistic_headbob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Target game build -------------------------------------------------------
# One .levipack per Minecraft build: the struct offsets are pinned here, so a
# package can never read the wrong layout. Layouts live in src/version/Profile.h.
set(MCVER "1.26.62" CACHE STRING "Minecraft Bedrock version (1.26.50 or 1.26.62)")

if(MCVER STREQUAL "1.26.50")
    set(HEADBOB_PROFILE_INDEX 0)
elseif(MCVER STREQUAL "1.26.62")
    set(HEADBOB_PROFILE_INDEX 1)
else()
    message(FATAL_ERROR "Unknown MCVER '${MCVER}'. Add a layout in src/version/Profile.h first.")
endif()

# ---- Mod metadata (override from command line) ----
set(MOD_ID                 "realistic_headbob"            CACHE STRING "Runtime mod id / folder name")
set(MOD_NAME               "Realistic Head Bob"           CACHE STRING "Display name")
set(MOD_AUTHOR             "qqchoke99"                    CACHE STRING "Author")
set(MOD_VERSION            "1.0.0-${MCVER}"               CACHE STRING "Version")
set(MOD_LIBRARY_NAME       "realistic_headbob"            CACHE STRING "Output .so base name (libXXX.so)")
set(MOD_MINECRAFT_VERSIONS "[\"${MCVER}.1\"]"             CACHE STRING "JSON array of supported MC versions")
set(MOD_DESCRIPTION        "FPS-style step head bob for Bedrock: camera dip and sway locked to your walk cycle, with idle breathing" CACHE STRING "")

# ---- Fetch preloader-android SDK ----
include(FetchContent)
FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG        0.2.2
)
FetchContent_MakeAvailable(preloader_android)

# ---- Sources ----
add_library(${MOD_LIBRARY_NAME} SHARED
    src/main.cpp
    src/mod/MyMod.cpp
    src/mod/Config.cpp
    src/mod/HeadBob.cpp
)

target_include_directories(${MOD_LIBRARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(${MOD_LIBRARY_NAME} PRIVATE HEADBOB_PROFILE_INDEX=${HEADBOB_PROFILE_INDEX})
target_link_libraries(${MOD_LIBRARY_NAME} PRIVATE preloader)

if(ANDROID)
    # pl::log::Logger goes through __android_log_print; the preloader links log
    # privately, so the mod has to pull it in itself.
    find_library(android_log_lib log REQUIRED)
    target_link_libraries(${MOD_LIBRARY_NAME} PRIVATE ${android_log_lib})
    target_compile_options(${MOD_LIBRARY_NAME} PRIVATE -fPIC -fvisibility=hidden)
    target_link_options(${MOD_LIBRARY_NAME} PRIVATE -Wl,-z,max-page-size=16384)
endif()

# ---- Generate manifest.json ----
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/manifest.json
    @ONLY
)

# ---- Package target ----
# make chdir()s into WORKING_DIRECTORY before running any command, so the
# directory has to exist at configure time.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/package)
add_custom_target(levi_package
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package/${MOD_ID}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${MOD_LIBRARY_NAME}> ${CMAKE_BINARY_DIR}/package/${MOD_ID}/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/manifest.json ${CMAKE_BINARY_DIR}/package/${MOD_ID}/
    # Pre-generated on a host (see config/README.md): a cross-compiled helper
    # cannot run on the build runner.
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/package/${MOD_ID}/config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config/config.json
            ${CMAKE_CURRENT_SOURCE_DIR}/config/config.schema.json
            ${CMAKE_BINARY_DIR}/package/${MOD_ID}/config/
    # cmake -E tar has no -C: archive from the package dir so paths stay
    # realistic_headbob/... instead of package/realistic_headbob/...
    COMMAND ${CMAKE_COMMAND} -E tar cf ${CMAKE_BINARY_DIR}/${MOD_ID}-${MCVER}.levipack
            --format=zip
            ${MOD_ID}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/package
    DEPENDS ${MOD_LIBRARY_NAME}
    COMMENT "Packaging ${MOD_ID}-${MCVER}.levipack"
)
