cmake_minimum_required(VERSION 3.24)

project(Novaria VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NOVARIA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(NOVARIA_BUILD_TESTS "Build unit tests." ON)
option(NOVARIA_ENABLE_LUAJIT "Enable LuaJIT backend when library is available." ON)
option(
    NOVARIA_ALLOW_SYSTEM_DEPS
    "Allow fallback to system package manager dependencies when third_party is incomplete."
    OFF
)

set(NOVARIA_VENDOR_DIR "${CMAKE_SOURCE_DIR}/third_party")
set(NOVARIA_LOCAL_SDL3_DIR "${NOVARIA_VENDOR_DIR}/SDL3-3.2.0/cmake")
set(NOVARIA_LOCAL_ENTT_INCLUDE_DIR "${NOVARIA_VENDOR_DIR}/entt-3.13.2/src")
set(NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR "${NOVARIA_VENDOR_DIR}/LuaJIT-2.1/src")
if(WIN32)
    set(NOVARIA_LOCAL_LUAJIT_LIBRARY "${NOVARIA_VENDOR_DIR}/LuaJIT-2.1/src/lua51.lib")
else()
    set(NOVARIA_LOCAL_LUAJIT_LIBRARY "${NOVARIA_VENDOR_DIR}/LuaJIT-2.1/src/libluajit-5.1.a")
endif()

set(SDL3_FOUND FALSE)
if(EXISTS "${NOVARIA_LOCAL_SDL3_DIR}/SDL3Config.cmake")
    find_package(SDL3 CONFIG QUIET PATHS "${NOVARIA_LOCAL_SDL3_DIR}" NO_DEFAULT_PATH)
endif()
if(NOT SDL3_FOUND AND NOVARIA_ALLOW_SYSTEM_DEPS)
    find_package(SDL3 CONFIG QUIET)
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
        "1) Install vendor dependency at third_party/SDL3-3.2.0.\n"
        "2) Or set -DNOVARIA_ALLOW_SYSTEM_DEPS=ON and provide SDL3 via system package manager.\n"
        "3) Or set SDL3_DIR manually.\n"
    )
endif()

set(NOVARIA_ENTT_TARGET "")
set(NOVARIA_ENTT_INCLUDE_DIR "")
if(EXISTS "${NOVARIA_LOCAL_ENTT_INCLUDE_DIR}/entt/entt.hpp")
    set(NOVARIA_ENTT_INCLUDE_DIR "${NOVARIA_LOCAL_ENTT_INCLUDE_DIR}")
elseif(NOVARIA_ALLOW_SYSTEM_DEPS)
    find_package(EnTT CONFIG QUIET)
    foreach(candidate_target EnTT::EnTT entt::entt)
        if(TARGET ${candidate_target})
            set(NOVARIA_ENTT_TARGET ${candidate_target})
            break()
        endif()
    endforeach()
endif()

if(NOT NOVARIA_ENTT_TARGET AND NOT NOVARIA_ENTT_INCLUDE_DIR)
    message(
        FATAL_ERROR
        "EnTT dependency not found.\n"
        "1) Install vendor dependency at third_party/entt-3.13.2/src.\n"
        "2) Or set -DNOVARIA_ALLOW_SYSTEM_DEPS=ON and provide EnTT via system package manager.\n"
        "3) Or set EnTT_DIR manually.\n"
    )
endif()

set(NOVARIA_LUAJIT_FOUND OFF)
if(NOVARIA_ENABLE_LUAJIT)
    set(novaria_luajit_include_candidate "")
    set(novaria_luajit_library_candidate "")

    if(DEFINED NOVARIA_LUAJIT_INCLUDE_DIR AND EXISTS "${NOVARIA_LUAJIT_INCLUDE_DIR}/lua.h")
        set(novaria_luajit_include_candidate "${NOVARIA_LUAJIT_INCLUDE_DIR}")
    elseif(EXISTS "${NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR}/lua.h")
        set(novaria_luajit_include_candidate "${NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR}")
    endif()

    if(DEFINED NOVARIA_LUAJIT_LIBRARY AND EXISTS "${NOVARIA_LUAJIT_LIBRARY}")
        set(novaria_luajit_library_candidate "${NOVARIA_LUAJIT_LIBRARY}")
    elseif(EXISTS "${NOVARIA_LOCAL_LUAJIT_LIBRARY}")
        set(novaria_luajit_library_candidate "${NOVARIA_LOCAL_LUAJIT_LIBRARY}")
    endif()

    if((NOT novaria_luajit_include_candidate OR NOT novaria_luajit_library_candidate) AND
       NOVARIA_ALLOW_SYSTEM_DEPS)
        if(NOT novaria_luajit_include_candidate)
            find_path(
                novaria_luajit_include_candidate
                NAMES lua.h luajit.h
                PATH_SUFFIXES luajit-2.1 lua5.1 lua
            )
        endif()

        if(NOT novaria_luajit_library_candidate)
            find_library(
                novaria_luajit_library_candidate
                NAMES luajit-5.1 luajit lua51
            )
        endif()
    endif()

    if(novaria_luajit_include_candidate AND novaria_luajit_library_candidate)
        set(
            NOVARIA_LUAJIT_INCLUDE_DIR
            "${novaria_luajit_include_candidate}"
            CACHE PATH
            "Path to LuaJIT headers."
            FORCE
        )
        set(
            NOVARIA_LUAJIT_LIBRARY
            "${novaria_luajit_library_candidate}"
            CACHE FILEPATH
            "Path to LuaJIT library."
            FORCE
        )
        set(NOVARIA_LUAJIT_FOUND ON)
        message(STATUS "LuaJIT found: ${NOVARIA_LUAJIT_LIBRARY}")
    else()
        message(
            STATUS
            "LuaJIT not found, script runtime will fail-fast at initialization. "
            "Expected vendor path: ${NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR}"
        )
    endif()
endif()

function(novaria_link_luajit_if_available target_name)
    if(NOVARIA_LUAJIT_FOUND)
        target_compile_definitions(${target_name} PRIVATE NOVARIA_WITH_LUAJIT=1)
        target_include_directories(${target_name} PRIVATE "${NOVARIA_LUAJIT_INCLUDE_DIR}")
        target_link_libraries(${target_name} PRIVATE "${NOVARIA_LUAJIT_LIBRARY}")
    endif()
endfunction()

function(novaria_link_entt target_name)
    if(NOVARIA_ENTT_TARGET)
        target_link_libraries(${target_name} PRIVATE ${NOVARIA_ENTT_TARGET})
    elseif(NOVARIA_ENTT_INCLUDE_DIR)
        target_include_directories(${target_name} PRIVATE "${NOVARIA_ENTT_INCLUDE_DIR}")
    endif()
endfunction()

function(novaria_link_winsock_if_needed target_name)
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE ws2_32)
    endif()
endfunction()

function(novaria_enable_default_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(NOVARIA_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(NOVARIA_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

add_executable(
    novaria
    src/main.cpp
    src/app/game_app.cpp
    src/app/game_loop.cpp
    src/app/input_command_mapper.cpp
    src/app/player_controller.cpp
    src/app/render_scene_builder.cpp
    src/sim/simulation_kernel.cpp
    src/sim/typed_command.cpp
    src/sim/ecs_runtime.cpp
    src/platform/sdl_context.cpp
    src/world/world_service_basic.cpp
    src/world/snapshot_codec.cpp
    src/net/net_service_runtime.cpp
    src/net/net_service_udp_loopback.cpp
    src/net/udp_transport.cpp
    src/script/script_host_runtime.cpp
    src/script/lua_jit_script_host.cpp
    src/mod/mod_loader.cpp
    src/save/save_repository.cpp
    src/core/logger.cpp
    src/core/config.cpp
)

target_include_directories(novaria PRIVATE src)
target_link_libraries(novaria PRIVATE ${NOVARIA_SDL3_TARGET})
novaria_link_entt(novaria)
novaria_link_winsock_if_needed(novaria)
novaria_link_luajit_if_available(novaria)

add_executable(
    novaria_net_smoke
    tools/net_smoke.cpp
    src/net/net_service_runtime.cpp
    src/net/net_service_udp_loopback.cpp
    src/net/udp_transport.cpp
    src/core/logger.cpp
)
target_include_directories(novaria_net_smoke PRIVATE src)
novaria_link_winsock_if_needed(novaria_net_smoke)

add_executable(
    novaria_net_soak
    tools/net_soak.cpp
    src/net/net_service_runtime.cpp
    src/net/net_service_udp_loopback.cpp
    src/net/udp_transport.cpp
    src/core/logger.cpp
)
target_include_directories(novaria_net_soak PRIVATE src)
novaria_link_winsock_if_needed(novaria_net_soak)

add_executable(
    novaria_server
    tools/server_main.cpp
    src/sim/simulation_kernel.cpp
    src/sim/typed_command.cpp
    src/sim/ecs_runtime.cpp
    src/world/world_service_basic.cpp
    src/world/snapshot_codec.cpp
    src/net/net_service_runtime.cpp
    src/net/net_service_udp_loopback.cpp
    src/net/udp_transport.cpp
    src/script/script_host_runtime.cpp
    src/script/lua_jit_script_host.cpp
    src/mod/mod_loader.cpp
    src/core/logger.cpp
    src/core/config.cpp
)
target_include_directories(novaria_server PRIVATE src)
novaria_link_entt(novaria_server)
novaria_link_luajit_if_available(novaria_server)
novaria_link_winsock_if_needed(novaria_server)

set(NOVARIA_LOCAL_SDL3_DLL "${NOVARIA_VENDOR_DIR}/SDL3-3.2.0/lib/x64/SDL3.dll")
if(EXISTS "${NOVARIA_LOCAL_SDL3_DLL}")
    add_custom_command(
        TARGET novaria
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${NOVARIA_LOCAL_SDL3_DLL}"
                "$<TARGET_FILE_DIR:novaria>/SDL3.dll"
    )
endif()

set(NOVARIA_RUNTIME_TARGETS
    novaria
    novaria_server
    novaria_net_smoke
    novaria_net_soak
)
foreach(runtime_target IN LISTS NOVARIA_RUNTIME_TARGETS)
    novaria_enable_default_warnings(${runtime_target})
endforeach()

if(NOVARIA_BUILD_TESTS)
    enable_testing()

    add_executable(
        novaria_config_tests
        tests/core/config_tests.cpp
        src/core/config.cpp
    )
    target_include_directories(novaria_config_tests PRIVATE src)

    add_executable(
        novaria_net_service_udp_loopback_tests
        tests/net/net_service_udp_loopback_tests.cpp
        src/net/net_service_udp_loopback.cpp
        src/net/udp_transport.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_net_service_udp_loopback_tests PRIVATE src)
    novaria_link_winsock_if_needed(novaria_net_service_udp_loopback_tests)

    add_executable(
        novaria_net_service_runtime_tests
        tests/net/net_service_runtime_tests.cpp
        src/net/net_service_runtime.cpp
        src/net/net_service_udp_loopback.cpp
        src/net/udp_transport.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_net_service_runtime_tests PRIVATE src)
    novaria_link_winsock_if_needed(novaria_net_service_runtime_tests)

    add_executable(
        novaria_udp_transport_tests
        tests/net/udp_transport_tests.cpp
        src/net/udp_transport.cpp
    )
    target_include_directories(novaria_udp_transport_tests PRIVATE src)
    novaria_link_winsock_if_needed(novaria_udp_transport_tests)

    add_executable(
        novaria_world_service_tests
        tests/world/world_service_basic_tests.cpp
        src/world/world_service_basic.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_world_service_tests PRIVATE src)

    add_executable(
        novaria_script_host_runtime_tests
        tests/script/script_host_runtime_tests.cpp
        src/script/script_host_runtime.cpp
        src/script/lua_jit_script_host.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_script_host_runtime_tests PRIVATE src)
    novaria_link_luajit_if_available(novaria_script_host_runtime_tests)

    add_executable(
        novaria_simulation_kernel_tests
        tests/sim/simulation_kernel_tests.cpp
        src/sim/simulation_kernel.cpp
        src/sim/typed_command.cpp
        src/sim/ecs_runtime.cpp
        src/world/snapshot_codec.cpp
        src/net/net_service_runtime.cpp
        src/net/net_service_udp_loopback.cpp
        src/net/udp_transport.cpp
        src/save/save_repository.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_simulation_kernel_tests PRIVATE src)
    novaria_link_entt(novaria_simulation_kernel_tests)
    novaria_link_winsock_if_needed(novaria_simulation_kernel_tests)

    add_executable(
        novaria_ecs_runtime_tests
        tests/sim/ecs_runtime_tests.cpp
        src/sim/ecs_runtime.cpp
    )
    target_include_directories(novaria_ecs_runtime_tests PRIVATE src)
    novaria_link_entt(novaria_ecs_runtime_tests)

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
        src/sim/typed_command.cpp
        src/sim/ecs_runtime.cpp
        src/world/world_service_basic.cpp
        src/world/snapshot_codec.cpp
        src/net/net_service_runtime.cpp
        src/net/net_service_udp_loopback.cpp
        src/net/udp_transport.cpp
        src/save/save_repository.cpp
        src/mod/mod_loader.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_mvp_acceptance_tests PRIVATE src)
    novaria_link_entt(novaria_mvp_acceptance_tests)
    novaria_link_winsock_if_needed(novaria_mvp_acceptance_tests)

    add_executable(
        novaria_gameplay_issue_e2e_tests
        tests/mvp/gameplay_issue_e2e_tests.cpp
        src/app/player_controller.cpp
        src/app/render_scene_builder.cpp
        src/sim/simulation_kernel.cpp
        src/sim/typed_command.cpp
        src/sim/ecs_runtime.cpp
        src/world/world_service_basic.cpp
        src/world/snapshot_codec.cpp
        src/net/net_service_runtime.cpp
        src/net/net_service_udp_loopback.cpp
        src/net/udp_transport.cpp
        src/core/logger.cpp
    )
    target_include_directories(novaria_gameplay_issue_e2e_tests PRIVATE src)
    novaria_link_entt(novaria_gameplay_issue_e2e_tests)
    novaria_link_winsock_if_needed(novaria_gameplay_issue_e2e_tests)

    set(NOVARIA_TEST_TARGETS
        novaria_config_tests
        novaria_net_service_udp_loopback_tests
        novaria_net_service_runtime_tests
        novaria_udp_transport_tests
        novaria_world_service_tests
        novaria_script_host_runtime_tests
        novaria_simulation_kernel_tests
        novaria_ecs_runtime_tests
        novaria_save_repository_tests
        novaria_mod_loader_tests
        novaria_world_snapshot_codec_tests
        novaria_world_replication_flow_tests
        novaria_mvp_acceptance_tests
        novaria_gameplay_issue_e2e_tests
    )
    foreach(test_target IN LISTS NOVARIA_TEST_TARGETS)
        novaria_enable_default_warnings(${test_target})
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()
endif()
