find_package(Qt6 REQUIRED COMPONENTS Test)

function(whispervtt_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE whispervtt_core Qt6::Test)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /W4 /permissive-)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # Find the Qt DLLs without deploying them, and print results even when
    # no console is attached (QtTest otherwise sends them to the debugger).
    set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_FORCE_STDERR_LOGGING=set:1")
endfunction()

whispervtt_add_test(tst_audioconvert)
whispervtt_add_test(tst_formatter)
whispervtt_add_test(tst_modelcatalog)
whispervtt_add_test(tst_recordingflow)
whispervtt_add_test(tst_textcleanup)
whispervtt_add_test(tst_transcriptlog)

# Integration test against the real Windows clipboard.
whispervtt_add_test(tst_clipboard)
target_sources(tst_clipboard PRIVATE ${PROJECT_SOURCE_DIR}/src/clipboard.cpp)
target_compile_definitions(tst_clipboard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(tst_clipboard PRIVATE user32)
