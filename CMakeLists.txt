cmake_minimum_required(VERSION 3.24)

project(Novaria VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Keep the CRT choice consistent with vendor libs (LuaJIT).
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

option(NOVARIA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(NOVARIA_BUILD_TESTS "Build unit tests." ON)
option(NOVARIA_BUILD_PERF_TESTS "Build perf/soak-gate tests." OFF)
option(NOVARIA_ENABLE_LUAJIT "Enable LuaJIT backend when library is available." ON)

set(NOVARIA_VENDOR_DIR "${CMAKE_SOURCE_DIR}/third_party")
set(NOVARIA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(NOVARIA_PUBLIC_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include/novaria")
file(MAKE_DIRECTORY "${NOVARIA_GENERATED_DIR}")
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/config/default.cfg"
        "${CMAKE_SOURCE_DIR}/src/script/bootstrap.lua"
)

file(READ "${CMAKE_SOURCE_DIR}/src/script/bootstrap.lua" NOVARIA_LUA_BOOTSTRAP_SCRIPT_RAW)
set(NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL "${NOVARIA_LUA_BOOTSTRAP_SCRIPT_RAW}")
string(REPLACE "\\" "\\\\" NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL "${NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL}")
string(REPLACE "\"" "\\\"" NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL "${NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL}")
string(REPLACE "\n" "\\n\"\n\"" NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL "${NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL}")
set(NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL "\"${NOVARIA_LUA_BOOTSTRAP_SCRIPT_C_LITERAL}\"")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/script/lua_bootstrap_script_embedded.h.in"
    "${NOVARIA_GENERATED_DIR}/lua_bootstrap_script_embedded.h"
    @ONLY
)

file(READ "${CMAKE_SOURCE_DIR}/config/default.cfg" NOVARIA_DEFAULT_CONFIG_RAW)
set(NOVARIA_DEFAULT_CONFIG_C_LITERAL "${NOVARIA_DEFAULT_CONFIG_RAW}")
string(REPLACE "\\" "\\\\" NOVARIA_DEFAULT_CONFIG_C_LITERAL "${NOVARIA_DEFAULT_CONFIG_C_LITERAL}")
string(REPLACE "\"" "\\\"" NOVARIA_DEFAULT_CONFIG_C_LITERAL "${NOVARIA_DEFAULT_CONFIG_C_LITERAL}")
string(REPLACE "\n" "\\n\"\n\"" NOVARIA_DEFAULT_CONFIG_C_LITERAL "${NOVARIA_DEFAULT_CONFIG_C_LITERAL}")
set(NOVARIA_DEFAULT_CONFIG_C_LITERAL "\"${NOVARIA_DEFAULT_CONFIG_C_LITERAL}\"")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/default_config_embedded.h.in"
    "${NOVARIA_GENERATED_DIR}/default_config_embedded.h"
    @ONLY
)

configure_file(
    "${CMAKE_SOURCE_DIR}/tools/cmake/dev_runtime.cfg.in"
    "${NOVARIA_GENERATED_DIR}/dev_runtime.cfg"
    @ONLY
)

set(NOVARIA_LOCAL_SDL3_DIR "${NOVARIA_VENDOR_DIR}/SDL3-3.2.0/cmake")
set(NOVARIA_LOCAL_ENTT_INCLUDE_DIR "${NOVARIA_VENDOR_DIR}/entt-3.13.2/src")
set(NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR "${NOVARIA_VENDOR_DIR}/LuaJIT-2.1/src")
if(WIN32)
    set(NOVARIA_LOCAL_LUAJIT_LIBRARY "${NOVARIA_VENDOR_DIR}/LuaJIT-2.1/src/lua51.lib")
else()
    set(NOVARIA_LOCAL_LUAJIT_LIBRARY "${NOVARIA_VENDOR_DIR}/LuaJIT-2.1/src/libluajit-5.1.a")
endif()

if(NOT EXISTS "${NOVARIA_LOCAL_SDL3_DIR}/SDL3Config.cmake")
    message(
        FATAL_ERROR
        "SDL3 vendor dependency not found.\n"
        "Install it at third_party/SDL3-3.2.0.\n"
    )
endif()
find_package(SDL3 CONFIG QUIET PATHS "${NOVARIA_LOCAL_SDL3_DIR}" NO_DEFAULT_PATH)

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
    )
endif()

if(EXISTS "${NOVARIA_LOCAL_ENTT_INCLUDE_DIR}/entt/entt.hpp")
    set(NOVARIA_ENTT_INCLUDE_DIR "${NOVARIA_LOCAL_ENTT_INCLUDE_DIR}")
endif()

if(NOT NOVARIA_ENTT_INCLUDE_DIR)
    message(
        FATAL_ERROR
        "EnTT dependency not found.\n"
        "Install it at third_party/entt-3.13.2/src.\n"
    )
endif()

set(NOVARIA_LUAJIT_FOUND OFF)
if(NOVARIA_ENABLE_LUAJIT)
    if(EXISTS "${NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR}/lua.h" AND EXISTS "${NOVARIA_LOCAL_LUAJIT_LIBRARY}")
        set(
            NOVARIA_LUAJIT_INCLUDE_DIR
            "${NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR}"
            CACHE PATH
            "Path to LuaJIT headers."
            FORCE
        )
        set(
            NOVARIA_LUAJIT_LIBRARY
            "${NOVARIA_LOCAL_LUAJIT_LIBRARY}"
            CACHE FILEPATH
            "Path to LuaJIT library."
            FORCE
        )
        set(NOVARIA_LUAJIT_FOUND ON)
        message(STATUS "LuaJIT found: ${NOVARIA_LUAJIT_LIBRARY}")
    else()
        message(
            FATAL_ERROR
            "LuaJIT dependency not found.\n"
            "Install it at third_party/LuaJIT-2.1/src.\n"
            "Expected vendor path: ${NOVARIA_LOCAL_LUAJIT_INCLUDE_DIR}\n"
        )
    endif()
endif()

function(novaria_link_luajit_if_available target_name)
    if(NOVARIA_LUAJIT_FOUND)
        target_compile_definitions(${target_name} PRIVATE NOVARIA_WITH_LUAJIT=1)
        target_include_directories(${target_name} PRIVATE "${NOVARIA_LUAJIT_INCLUDE_DIR}")
        target_link_libraries(${target_name} PUBLIC "${NOVARIA_LUAJIT_LIBRARY}")
    endif()
endfunction()

function(novaria_link_entt target_name)
    target_include_directories(${target_name} PRIVATE "${NOVARIA_ENTT_INCLUDE_DIR}")
endfunction()

function(novaria_link_winsock_if_needed target_name)
    if(WIN32)
        target_link_libraries(${target_name} PUBLIC ws2_32)
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

add_library(
    novaria_core
    STATIC
    src/core/executable_path.cpp
    src/core/logger.cpp
    src/core/cfg_parser.cpp
    src/core/config.cpp
    src/core/sha256.cpp
    src/core/base64.cpp
)
target_include_directories(
    novaria_core
    PUBLIC
        "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    PRIVATE
        src
        "${NOVARIA_GENERATED_DIR}"
)

add_library(
    novaria_wire
    STATIC
    src/wire/byte_io.cpp
    src/wire/envelope.cpp
)
target_include_directories(novaria_wire PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}")

add_library(
    novaria_world
    STATIC
    src/world/material_catalog.cpp
    src/world/snapshot_codec.cpp
)
target_include_directories(novaria_world PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria_world PUBLIC novaria_wire)

add_library(
    novaria_world_basic
    STATIC
    src/world/world_service_basic.cpp
)
target_include_directories(novaria_world_basic PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}" PRIVATE src)
target_link_libraries(novaria_world_basic PUBLIC novaria_world novaria_core)

add_library(
    novaria_net_udp_peer
    STATIC
    src/net/net_service_udp_peer.cpp
    src/net/udp_transport.cpp
)
target_include_directories(novaria_net_udp_peer PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}" PRIVATE src)
target_link_libraries(novaria_net_udp_peer PUBLIC novaria_wire novaria_core)
novaria_link_winsock_if_needed(novaria_net_udp_peer)

add_library(
    novaria_script_lua_jit
    STATIC
    src/script/lua_jit_script_host.cpp
)
target_include_directories(
    novaria_script_lua_jit
    PUBLIC
        "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    PRIVATE
        src
        "${NOVARIA_GENERATED_DIR}"
)
target_link_libraries(novaria_script_lua_jit PUBLIC novaria_world novaria_wire novaria_core)
novaria_link_luajit_if_available(novaria_script_lua_jit)

add_library(
    novaria_mod
    STATIC
    src/mod/mod_loader.cpp
)
target_include_directories(novaria_mod PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}" PRIVATE src)
target_link_libraries(novaria_mod PUBLIC novaria_core)

add_library(
    novaria_save
    STATIC
    src/save/save_repository.cpp
)
target_include_directories(novaria_save PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}" PRIVATE src)
target_link_libraries(novaria_save PUBLIC novaria_wire novaria_core)

add_library(
    novaria_sim
    STATIC
    src/sim/simulation_kernel.cpp
    src/sim/player_motion.cpp
    src/sim/tile_collision.cpp
    src/sim/gameplay_ruleset.cpp
    src/sim/command_schema.cpp
    src/sim/typed_command.cpp
    src/sim/ecs_runtime.cpp
)
target_include_directories(novaria_sim PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}" PRIVATE src)
target_link_libraries(novaria_sim PUBLIC novaria_world novaria_wire novaria_core)
novaria_link_entt(novaria_sim)

add_library(
    novaria_runtime
    STATIC
    src/runtime/runtime_paths.cpp
    src/runtime/mod_script_loader.cpp
    src/runtime/world_service_factory.cpp
    src/runtime/mod_pipeline.cpp
    src/runtime/save_state_loader.cpp
    src/runtime/mod_fingerprint_policy.cpp
    src/runtime/net_service_factory.cpp
    src/runtime/script_host_factory.cpp
)
target_include_directories(novaria_runtime PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}" PRIVATE src)
target_link_libraries(
    novaria_runtime
    PUBLIC
        novaria_mod
        novaria_save
        novaria_sim
        novaria_world
        novaria_wire
        novaria_core
        novaria_world_basic
        novaria_net_udp_peer
        novaria_script_lua_jit
)

add_library(
    novaria_platform
    STATIC
    src/platform/sdl_context.cpp
)
target_include_directories(novaria_platform PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria_platform PUBLIC novaria_core ${NOVARIA_SDL3_TARGET})

add_library(
    novaria_app
    STATIC
    src/app/game_app.cpp
    src/app/game_loop.cpp
    src/app/input_command_mapper.cpp
    src/app/player_controller_components.cpp
    src/app/player_controller.cpp
    src/app/render_scene_builder.cpp
)
target_include_directories(novaria_app PUBLIC "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria_app PUBLIC novaria_runtime novaria_platform)

add_library(
    novaria_engine
    INTERFACE
)
target_link_libraries(
    novaria_engine
    INTERFACE
        novaria_runtime
        novaria_app
        novaria_platform
        novaria_sim
        novaria_world
        novaria_wire
        novaria_core
)

add_executable(
    novaria
    src/main.cpp
)

target_include_directories(novaria PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria PRIVATE novaria_app)

add_executable(
    novaria_net_smoke
    tools/net_smoke.cpp
)
target_include_directories(novaria_net_smoke PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria_net_smoke PRIVATE novaria_engine)
novaria_link_winsock_if_needed(novaria_net_smoke)

add_executable(
    novaria_net_soak
    tools/net_soak.cpp
)
target_include_directories(novaria_net_soak PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria_net_soak PRIVATE novaria_engine)
novaria_link_winsock_if_needed(novaria_net_soak)

add_executable(
    novaria_simrpc_codegen
    tools/simrpc_codegen.cpp
)
target_include_directories(novaria_simrpc_codegen PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")

add_executable(
    novaria_server
    tools/server_main.cpp
)
target_include_directories(novaria_server PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
target_link_libraries(novaria_server PRIVATE novaria_engine)

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

add_custom_command(
    TARGET novaria
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NOVARIA_GENERATED_DIR}/dev_runtime.cfg"
            "$<TARGET_FILE_DIR:novaria>/novaria.cfg"
)

add_custom_command(
    TARGET novaria_server
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${NOVARIA_GENERATED_DIR}/dev_runtime.cfg"
            "$<TARGET_FILE_DIR:novaria_server>/novaria_server.cfg"
)

set(NOVARIA_RUNTIME_TARGETS
    novaria
    novaria_server
    novaria_net_smoke
    novaria_net_soak
)
foreach(runtime_target IN LISTS NOVARIA_RUNTIME_TARGETS)
    novaria_enable_default_warnings(${runtime_target})
endforeach()

set(NOVARIA_LIBRARY_TARGETS
    novaria_core
    novaria_wire
    novaria_world
    novaria_world_basic
    novaria_net_udp_peer
    novaria_script_lua_jit
    novaria_mod
    novaria_save
    novaria_sim
    novaria_runtime
    novaria_platform
    novaria_app
)
foreach(library_target IN LISTS NOVARIA_LIBRARY_TARGETS)
    novaria_enable_default_warnings(${library_target})
endforeach()

if(NOVARIA_BUILD_TESTS)
    enable_testing()

    add_executable(
        novaria_config_tests
        tests/core/config_tests.cpp
    )
    target_include_directories(novaria_config_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_config_tests PRIVATE novaria_engine)

    add_executable(
        novaria_net_service_udp_peer_tests
        tests/net/net_service_udp_peer_tests.cpp
    )
    target_include_directories(
        novaria_net_service_udp_peer_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
            "${CMAKE_SOURCE_DIR}/src"
    )
    target_link_libraries(novaria_net_service_udp_peer_tests PRIVATE novaria_engine)
    novaria_link_winsock_if_needed(novaria_net_service_udp_peer_tests)

    add_executable(
        novaria_net_service_runtime_tests
        tests/net/net_service_runtime_tests.cpp
    )
    target_include_directories(
        novaria_net_service_runtime_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    )
    target_link_libraries(novaria_net_service_runtime_tests PRIVATE novaria_engine)
    novaria_link_winsock_if_needed(novaria_net_service_runtime_tests)

    add_executable(
        novaria_udp_transport_tests
        tests/net/udp_transport_tests.cpp
    )
    target_include_directories(novaria_udp_transport_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_udp_transport_tests PRIVATE novaria_engine)
    novaria_link_winsock_if_needed(novaria_udp_transport_tests)

    add_executable(
        novaria_world_service_tests
        tests/world/world_service_basic_tests.cpp
    )
    target_include_directories(novaria_world_service_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_world_service_tests PRIVATE novaria_engine)

    add_executable(
        novaria_player_controller_components_tests
        tests/app/player_controller_components_tests.cpp
    )
    target_include_directories(
        novaria_player_controller_components_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    )
    target_link_libraries(novaria_player_controller_components_tests PRIVATE novaria_engine novaria_app)
    novaria_link_entt(novaria_player_controller_components_tests)

    add_executable(
        novaria_render_scene_builder_tests
        tests/app/render_scene_builder_tests.cpp
    )
    target_include_directories(novaria_render_scene_builder_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_render_scene_builder_tests PRIVATE novaria_engine novaria_app)
    novaria_link_entt(novaria_render_scene_builder_tests)

    add_executable(
        novaria_script_host_runtime_tests
        tests/script/script_host_runtime_tests.cpp
    )
    target_include_directories(
        novaria_script_host_runtime_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
            "${NOVARIA_GENERATED_DIR}"
    )
    target_link_libraries(novaria_script_host_runtime_tests PRIVATE novaria_engine)
    novaria_link_luajit_if_available(novaria_script_host_runtime_tests)

    add_executable(
        novaria_simulation_kernel_tests
        tests/sim/simulation_kernel_tests.cpp
    )
    target_include_directories(novaria_simulation_kernel_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_include_directories(novaria_simulation_kernel_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(novaria_simulation_kernel_tests PRIVATE novaria_engine)
    novaria_link_entt(novaria_simulation_kernel_tests)
    novaria_link_winsock_if_needed(novaria_simulation_kernel_tests)

    add_executable(
        novaria_player_motion_tests
        tests/sim/player_motion_tests.cpp
    )
    target_include_directories(novaria_player_motion_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_player_motion_tests PRIVATE novaria_engine)

    add_executable(
        novaria_ecs_runtime_tests
        tests/sim/ecs_runtime_tests.cpp
    )
    target_include_directories(novaria_ecs_runtime_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_ecs_runtime_tests PRIVATE novaria_engine)
    novaria_link_entt(novaria_ecs_runtime_tests)

    add_executable(
        novaria_save_repository_tests
        tests/save/save_repository_tests.cpp
    )
    target_include_directories(novaria_save_repository_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_save_repository_tests PRIVATE novaria_engine)

    add_executable(
        novaria_mod_loader_tests
        tests/mod/mod_loader_tests.cpp
    )
    target_include_directories(novaria_mod_loader_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_mod_loader_tests PRIVATE novaria_engine)

    add_executable(
        novaria_mod_script_loader_tests
        tests/runtime/mod_script_loader_tests.cpp
    )
    target_include_directories(novaria_mod_script_loader_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_mod_script_loader_tests PRIVATE novaria_engine)

    add_executable(
        novaria_mod_fingerprint_policy_tests
        tests/runtime/mod_fingerprint_policy_tests.cpp
    )
    target_include_directories(novaria_mod_fingerprint_policy_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_mod_fingerprint_policy_tests PRIVATE novaria_engine)

    add_executable(
        novaria_world_snapshot_codec_tests
        tests/world/world_snapshot_codec_tests.cpp
    )
    target_include_directories(
        novaria_world_snapshot_codec_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    )
    target_link_libraries(novaria_world_snapshot_codec_tests PRIVATE novaria_engine)

    add_executable(
        novaria_world_replication_flow_tests
        tests/world/world_replication_flow_tests.cpp
    )
    target_include_directories(
        novaria_world_replication_flow_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    )
    target_link_libraries(novaria_world_replication_flow_tests PRIVATE novaria_engine)

    add_executable(
        novaria_mvp_acceptance_tests
        tests/mvp/mvp_acceptance_tests.cpp
    )
    target_include_directories(novaria_mvp_acceptance_tests PRIVATE "${NOVARIA_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(novaria_mvp_acceptance_tests PRIVATE novaria_engine)
    novaria_link_entt(novaria_mvp_acceptance_tests)
    novaria_link_winsock_if_needed(novaria_mvp_acceptance_tests)

    add_executable(
        novaria_gameplay_issue_e2e_tests
        tests/mvp/gameplay_issue_e2e_tests.cpp
    )
    target_include_directories(
        novaria_gameplay_issue_e2e_tests
        PRIVATE
            "${NOVARIA_PUBLIC_INCLUDE_DIR}"
    )
    target_link_libraries(novaria_gameplay_issue_e2e_tests PRIVATE novaria_engine novaria_app)
    novaria_link_entt(novaria_gameplay_issue_e2e_tests)
    novaria_link_winsock_if_needed(novaria_gameplay_issue_e2e_tests)

    set(NOVARIA_TEST_TARGETS
        novaria_config_tests
        novaria_net_service_udp_peer_tests
        novaria_net_service_runtime_tests
        novaria_udp_transport_tests
        novaria_world_service_tests
        novaria_player_controller_components_tests
        novaria_render_scene_builder_tests
        novaria_script_host_runtime_tests
        novaria_simulation_kernel_tests
        novaria_player_motion_tests
        novaria_ecs_runtime_tests
        novaria_save_repository_tests
        novaria_mod_loader_tests
        novaria_mod_script_loader_tests
        novaria_mod_fingerprint_policy_tests
        novaria_world_snapshot_codec_tests
        novaria_world_replication_flow_tests
        novaria_gameplay_issue_e2e_tests
    )
    if(NOVARIA_BUILD_PERF_TESTS)
        list(APPEND NOVARIA_TEST_TARGETS novaria_mvp_acceptance_tests)
    endif()
    foreach(test_target IN LISTS NOVARIA_TEST_TARGETS)
        novaria_enable_default_warnings(${test_target})
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()
    if(NOVARIA_BUILD_PERF_TESTS)
        set_tests_properties(novaria_mvp_acceptance_tests PROPERTIES LABELS "perf")
    endif()
endif()
