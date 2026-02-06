cmake_minimum_required(VERSION 3.24)

project(Novaria VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NOVARIA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(NOVARIA_FETCH_SDL3 "Fetch SDL3 when local package is unavailable." OFF)
option(NOVARIA_BUILD_TESTS "Build unit tests." ON)

set(NOVARIA_LOCAL_SDL3_DIR "${CMAKE_SOURCE_DIR}/third_party/SDL3-3.2.0/cmake")
if(NOT DEFINED SDL3_DIR AND EXISTS "${NOVARIA_LOCAL_SDL3_DIR}/SDL3Config.cmake")
    set(SDL3_DIR "${NOVARIA_LOCAL_SDL3_DIR}" CACHE PATH "Path to SDL3Config.cmake directory.")
endif()

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND AND NOVARIA_FETCH_SDL3)
    include(FetchContent)
    FetchContent_Declare(
        SDL3
        URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(SDL3)
endif()

set(NOVARIA_SDL3_TARGET "")
foreach(candidate_target SDL3::SDL3 SDL3::SDL3-shared SDL3::SDL3-static)
    if(TARGET ${candidate_target})
        set(NOVARIA_SDL3_TARGET ${candidate_target})
        break()
    endif()
endforeach()

if(NOT NOVARIA_SDL3_TARGET)
    message(
        FATAL_ERROR
        "SDL3 target not found.\n"
        "1) Install SDL3 with your package manager (vcpkg/msys2).\n"
        "2) Set SDL3_DIR manually.\n"
        "3) If network allows, configure with -DNOVARIA_FETCH_SDL3=ON.\n"
    )
endif()

add_executable(
    novaria
    src/main.cpp
    src/app/game_app.cpp
    src/app/game_loop.cpp
    src/sim/simulation_kernel.cpp
    src/platform/sdl_context.cpp
    src/world/world_service_basic.cpp
    src/world/snapshot_codec.cpp
    src/net/net_service_stub.cpp
    src/script/script_host_stub.cpp
    src/mod/mod_loader.cpp
    src/save/save_repository.cpp
    src/core/logger.cpp
    src/core/config.cpp
)

target_include_directories(novaria PRIVATE src)
target_link_libraries(novaria PRIVATE ${NOVARIA_SDL3_TARGET})

set(NOVARIA_LOCAL_SDL3_DLL "${CMAKE_SOURCE_DIR}/third_party/SDL3-3.2.0/lib/x64/SDL3.dll")
if(EXISTS "${NOVARIA_LOCAL_SDL3_DLL}")
    add_custom_command(
        TARGET novaria
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NOVARIA_LOCAL_SDL3_DLL}"
                "$<TARGET_FILE_DIR:novaria>/SDL3.dll"
    )
endif()

if(MSVC)
    target_compile_options(novaria PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(NOVARIA_WARNINGS_AS_ERRORS)
        target_compile_options(novaria PRIVATE /WX)
    endif()
else()
    target_compile_options(novaria PRIVATE -Wall -Wextra -Wpedantic)
    if(NOVARIA_WARNINGS_AS_ERRORS)
        target_compile_options(novaria PRIVATE -Werror)
    endif()
endif()

if(NOVARIA_BUILD_TESTS)
    enable_testing()

    add_executable(
        novaria_config_tests
        tests/core/config_tests.cpp
        src/core/config.cpp
    )
    target_include_directories(novaria_config_tests PRIVATE src)

    add_executable(
        novaria_net_service_stub_tests
        tests/net/net_service_stub_tests.cpp
        src/net/net_service_stub.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_net_service_stub_tests PRIVATE src)

    add_executable(
        novaria_world_service_tests
        tests/world/world_service_basic_tests.cpp
        src/world/world_service_basic.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_world_service_tests PRIVATE src)

    add_executable(
        novaria_script_host_stub_tests
        tests/script/script_host_stub_tests.cpp
        src/script/script_host_stub.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_script_host_stub_tests PRIVATE src)

    add_executable(
        novaria_simulation_kernel_tests
        tests/sim/simulation_kernel_tests.cpp
        src/sim/simulation_kernel.cpp
        src/world/snapshot_codec.cpp
        src/net/net_service_stub.cpp
        src/save/save_repository.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_simulation_kernel_tests PRIVATE src)

    add_executable(
        novaria_save_repository_tests
        tests/save/save_repository_tests.cpp
        src/save/save_repository.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_save_repository_tests PRIVATE src)

    add_executable(
        novaria_mod_loader_tests
        tests/mod/mod_loader_tests.cpp
        src/mod/mod_loader.cpp
    )
    target_include_directories(novaria_mod_loader_tests PRIVATE src)

    add_executable(
        novaria_world_snapshot_codec_tests
        tests/world/world_snapshot_codec_tests.cpp
        src/world/snapshot_codec.cpp
    )
    target_include_directories(novaria_world_snapshot_codec_tests PRIVATE src)

    add_executable(
        novaria_world_replication_flow_tests
        tests/world/world_replication_flow_tests.cpp
        src/world/world_service_basic.cpp
        src/world/snapshot_codec.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_world_replication_flow_tests PRIVATE src)

    add_executable(
        novaria_mvp_acceptance_tests
        tests/mvp/mvp_acceptance_tests.cpp
        src/sim/simulation_kernel.cpp
        src/world/world_service_basic.cpp
        src/world/snapshot_codec.cpp
        src/net/net_service_stub.cpp
        src/script/script_host_stub.cpp
        src/save/save_repository.cpp
        src/mod/mod_loader.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_mvp_acceptance_tests PRIVATE src)

    if(MSVC)
        target_compile_options(novaria_config_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_net_service_stub_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_world_service_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_script_host_stub_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_simulation_kernel_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_save_repository_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_mod_loader_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_world_snapshot_codec_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_world_replication_flow_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        target_compile_options(novaria_mvp_acceptance_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(NOVARIA_WARNINGS_AS_ERRORS)
            target_compile_options(novaria_config_tests PRIVATE /WX)
            target_compile_options(novaria_net_service_stub_tests PRIVATE /WX)
            target_compile_options(novaria_world_service_tests PRIVATE /WX)
            target_compile_options(novaria_script_host_stub_tests PRIVATE /WX)
            target_compile_options(novaria_simulation_kernel_tests PRIVATE /WX)
            target_compile_options(novaria_save_repository_tests PRIVATE /WX)
            target_compile_options(novaria_mod_loader_tests PRIVATE /WX)
            target_compile_options(novaria_world_snapshot_codec_tests PRIVATE /WX)
            target_compile_options(novaria_world_replication_flow_tests PRIVATE /WX)
            target_compile_options(novaria_mvp_acceptance_tests PRIVATE /WX)
        endif()
    else()
        target_compile_options(novaria_config_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_net_service_stub_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_world_service_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_script_host_stub_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_simulation_kernel_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_save_repository_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_mod_loader_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_world_snapshot_codec_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_world_replication_flow_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(novaria_mvp_acceptance_tests PRIVATE -Wall -Wextra -Wpedantic)
        if(NOVARIA_WARNINGS_AS_ERRORS)
            target_compile_options(novaria_config_tests PRIVATE -Werror)
            target_compile_options(novaria_net_service_stub_tests PRIVATE -Werror)
            target_compile_options(novaria_world_service_tests PRIVATE -Werror)
            target_compile_options(novaria_script_host_stub_tests PRIVATE -Werror)
            target_compile_options(novaria_simulation_kernel_tests PRIVATE -Werror)
            target_compile_options(novaria_save_repository_tests PRIVATE -Werror)
            target_compile_options(novaria_mod_loader_tests PRIVATE -Werror)
            target_compile_options(novaria_world_snapshot_codec_tests PRIVATE -Werror)
            target_compile_options(novaria_world_replication_flow_tests PRIVATE -Werror)
            target_compile_options(novaria_mvp_acceptance_tests PRIVATE -Werror)
        endif()
    endif()

    add_test(NAME novaria_config_tests COMMAND novaria_config_tests)
    add_test(NAME novaria_net_service_stub_tests COMMAND novaria_net_service_stub_tests)
    add_test(NAME novaria_world_service_tests COMMAND novaria_world_service_tests)
    add_test(NAME novaria_script_host_stub_tests COMMAND novaria_script_host_stub_tests)
    add_test(NAME novaria_simulation_kernel_tests COMMAND novaria_simulation_kernel_tests)
    add_test(NAME novaria_save_repository_tests COMMAND novaria_save_repository_tests)
    add_test(NAME novaria_mod_loader_tests COMMAND novaria_mod_loader_tests)
    add_test(NAME novaria_world_snapshot_codec_tests COMMAND novaria_world_snapshot_codec_tests)
    add_test(NAME novaria_world_replication_flow_tests COMMAND novaria_world_replication_flow_tests)
    add_test(NAME novaria_mvp_acceptance_tests COMMAND novaria_mvp_acceptance_tests)
endif()
