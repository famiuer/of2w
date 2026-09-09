cmake_minimum_required(VERSION 3.16)

project(oc4_coupling LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OC4_COUPLING_BUILD_TESTS "Build coupling unit tests" ON)

add_library(coupling_common
    common/src/Formatting.cpp
)

target_include_directories(coupling_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

target_compile_features(coupling_common PUBLIC cxx_std_17)

add_library(coupling_moordyn
    moordyn/src/MoorDynCApiAdapter.cpp
)
target_include_directories(coupling_moordyn
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/moordyn/include
)
target_link_libraries(coupling_moordyn
    PUBLIC
        coupling_common
        ${CMAKE_DL_LIBS}
)
target_compile_features(coupling_moordyn PUBLIC cxx_std_17)

add_library(coupling_openfast
    openfast/src/ForcedOpenFASTCApiAdapter.cpp
)
target_include_directories(coupling_openfast
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/openfast/include
)
target_link_libraries(coupling_openfast
    PUBLIC
        coupling_common
        ${CMAKE_DL_LIBS}
)
target_compile_features(coupling_openfast PUBLIC cxx_std_17)

add_library(forcedopenfast_runtime SHARED
    openfast/runtime/src/ForcedOpenFASTLibrary.cpp
)
target_include_directories(forcedopenfast_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/openfast/include
)
target_compile_features(forcedopenfast_runtime PUBLIC cxx_std_17)
target_link_libraries(forcedopenfast_runtime PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(forcedopenfast_runtime PROPERTIES
    OUTPUT_NAME forcedopenfast
)

add_library(fake_openfast_library SHARED
    openfast/tests/fake_openfast_library.cpp
)
set_target_properties(fake_openfast_library PROPERTIES
    OUTPUT_NAME fakeopenfastlib
)

add_library(coupling_restraints
    restraints/src/MoorDynRestraintBridge.cpp
    restraints/src/MoorDynLoadProvider.cpp
    restraints/src/OpenFASTLoadProvider.cpp
)
target_include_directories(coupling_restraints
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/restraints/include
)
target_link_libraries(coupling_restraints
    PUBLIC
        coupling_common
        coupling_moordyn
        coupling_openfast
)
target_compile_features(coupling_restraints PUBLIC cxx_std_17)

if(OC4_COUPLING_BUILD_TESTS)
    enable_testing()

    add_executable(test_coupling_common
        common/tests/test_common.cpp
    )

    target_link_libraries(test_coupling_common PRIVATE coupling_common)

    add_test(NAME coupling_common COMMAND test_coupling_common)

    add_executable(test_coupling_moordyn
        moordyn/tests/test_moordyn_adapter.cpp
    )

    target_link_libraries(test_coupling_moordyn PRIVATE coupling_moordyn)

    add_test(NAME coupling_moordyn COMMAND test_coupling_moordyn)

    add_executable(test_coupling_openfast
        openfast/tests/test_forced_openfast_adapter.cpp
    )

    target_link_libraries(test_coupling_openfast PRIVATE coupling_openfast)

    add_test(NAME coupling_openfast COMMAND test_coupling_openfast)

    add_executable(test_coupling_openfast_runtime
        openfast/tests/test_forced_openfast_runtime.cpp
    )

    target_link_libraries(test_coupling_openfast_runtime PRIVATE forcedopenfast_runtime)

    add_test(
        NAME coupling_openfast_runtime
        COMMAND
            test_coupling_openfast_runtime
            ${CMAKE_CURRENT_SOURCE_DIR}/config/examples/openfast_smoke
    )

    add_executable(test_coupling_openfast_runtime_adapter
        openfast/tests/test_forced_openfast_runtime_adapter.cpp
    )

    target_link_libraries(test_coupling_openfast_runtime_adapter PRIVATE coupling_openfast)

    add_test(
        NAME coupling_openfast_runtime_adapter
        COMMAND
            test_coupling_openfast_runtime_adapter
            ${CMAKE_CURRENT_SOURCE_DIR}/config/examples/openfast_smoke
            $<TARGET_FILE:forcedopenfast_runtime>
    )

    add_executable(test_coupling_openfast_library_backend
        openfast/tests/test_forced_openfast_openfast_library_backend.cpp
    )

    target_link_libraries(test_coupling_openfast_library_backend PRIVATE forcedopenfast_runtime)

    add_test(
        NAME coupling_openfast_library_backend
        COMMAND
            test_coupling_openfast_library_backend
            $<TARGET_FILE:fake_openfast_library>
    )

    add_executable(test_coupling_restraints
        restraints/tests/test_moordyn_load_provider.cpp
    )

    target_link_libraries(test_coupling_restraints PRIVATE coupling_restraints)

    add_test(NAME coupling_restraints COMMAND test_coupling_restraints)

    add_executable(test_coupling_restraint_bridge
        restraints/tests/test_moordyn_restraint_bridge.cpp
    )

    target_link_libraries(test_coupling_restraint_bridge PRIVATE coupling_restraints)

    add_test(NAME coupling_restraint_bridge COMMAND test_coupling_restraint_bridge)

    add_executable(test_coupling_openfast_load_provider
        restraints/tests/test_openfast_load_provider.cpp
    )

    target_link_libraries(test_coupling_openfast_load_provider PRIVATE coupling_restraints)

    add_test(NAME coupling_openfast_load_provider COMMAND test_coupling_openfast_load_provider)
endif()
