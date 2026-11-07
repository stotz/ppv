# Unit tests of the portable core
foreach(name format meter)
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

add_test(NAME e2e.pipe  COMMAND ${_e2e} -DMODE=pipe  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
add_test(NAME e2e.file  COMMAND ${_e2e} -DMODE=file  -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
add_test(NAME e2e.empty COMMAND ${_e2e} -DMODE=empty -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
add_test(NAME e2e.closed COMMAND ${_e2e} -DMODE=closed -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
add_test(NAME e2e.usage COMMAND ${_e2e} -DMODE=usage -P ${CMAKE_CURRENT_SOURCE_DIR}/e2e.cmake)
