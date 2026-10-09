# Test executable — uses library sources from parent scope (LIB_SOURCES)
# LIB_SOURCES contains paths relative to the project root, so prepend
# CMAKE_SOURCE_DIR to make them absolute for this subdirectory context.
set(ABSOLUTE_LIB_SOURCES)
foreach(src ${LIB_SOURCES})
    list(APPEND ABSOLUTE_LIB_SOURCES "${CMAKE_SOURCE_DIR}/${src}")
endforeach()

set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_dwindle.cpp
    tests/test_dwindle_tree.cpp
    tests/test_monocle.cpp
    tests/test_master.cpp
    tests/test_keystroke.cpp
    tests/test_keybind_dictionary.cpp
    tests/test_lua_config.cpp
    tests/test_bar_indicators.cpp
    tests/test_exec_cache.cpp
    tests/test_bfwm_action.cpp
    tests/test_window_registry.cpp
    tests/test_action_dispatcher.cpp
    tests/test_workspace_core.cpp
    tests/test_bfwm_transaction.cpp
    tests/test_keystroke_queue.cpp
    tests/test_spsc_queue.cpp
    tests/test_window_rule.cpp
    tests/test_overlay_reconcile.cpp
    tests/test_overlay_strip_geometry.cpp
    tests/test_placement_classify.cpp
    tests/test_float_drag_classify.cpp
    tests/test_float_clamp.cpp
    tests/test_dpi_scale.cpp
    tests/test_layout_insets.cpp
)

add_executable(test_runner ${ABSOLUTE_LIB_SOURCES} ${TEST_SOURCES})
target_compile_definitions(test_runner PRIVATE BFWM_BUILD_TESTING)
target_link_libraries(test_runner PRIVATE dwmapi gdiplus lua tiny-regex-c clay ole32 oleaut32 psapi wlanapi shcore)
target_include_directories(test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cbelt)
message(STATUS "Created test executable: test_runner")

enable_testing()
add_test(NAME tests COMMAND test_runner)

add_custom_target(test-run
    COMMAND $<TARGET_FILE:test_runner>
    DEPENDS test_runner
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running tests"
    USES_TERMINAL
)
