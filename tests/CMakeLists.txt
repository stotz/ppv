# Unit tests of the portable core (no OS dependencies)
foreach(name format meter options display report)
  add_executable(test_${name} test_${name}.c)
  target_link_libraries(test_${name} PRIVATE ppv_core)
  add_test(NAME unit.${name} COMMAND test_${name})
endforeach()

# Platform layer: process I/O counters (--pid). Exit code 77 = skipped
# (e.g. Linux kernel without I/O accounting such as WSL2, or macOS).
add_executable(test_proc test_proc.c)
target_link_libraries(test_proc PRIVATE ppv_platform)
add_test(NAME platform.proc COMMAND test_proc)
set_tests_properties(platform.proc PROPERTIES SKIP_RETURN_CODE 77)

# Helpers for the end-to-end tests: a deterministic binary data source/sink
add_executable(ppv_gen gen.c)
add_executable(ppv_check check.c)
foreach(t ppv_gen ppv_check)
  target_link_libraries(${t} PRIVATE ppv_settings)
endforeach()

set(_e2e ${CMAKE_COMMAND}
  -DPPV=$<TARGET_FILE:ppv>
  -DGEN=$<TARGET_FILE:ppv_gen>
  -DCHECK=$<TARGET_FILE:ppv_check>
  -DWORK=${CMAKE_CURRENT_BINARY_DIR})

foreach(mode pipe file empty closed live wait buffer usage)
  add_test(NAME e2e.${mode}
    COMMAND ${_e2e} -DMODE=${mode} -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
endforeach()

# Process harnesses: spawn ppv and helpers as child processes.
#   test_pid:   ppv --pid on a slow producer (Linux, Windows; 77 = skipped elsewhere)
#   test_posix: closed standard descriptors, Ctrl-C / SIGTERM, O_APPEND output
add_executable(test_pid test_pid.c)
target_link_libraries(test_pid PRIVATE ppv_settings)
add_test(NAME e2e.pid COMMAND test_pid $<TARGET_FILE:ppv> $<TARGET_FILE:ppv_gen>
  ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(e2e.pid PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

if(NOT WIN32)
  add_executable(test_posix test_posix.c)
  target_link_libraries(test_posix PRIVATE ppv_settings)
  add_test(NAME e2e.posix COMMAND test_posix $<TARGET_FILE:ppv> $<TARGET_FILE:ppv_gen>
    $<TARGET_FILE:ppv_check> ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(e2e.posix PROPERTIES TIMEOUT 60)
endif()
