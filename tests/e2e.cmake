# End-to-end tests, run with: cmake -DPPV=... -DGEN=... -DCHECK=... -DWORK=... -DMODE=... -P e2e.cmake

# 48 MiB + an odd tail, so the last read is a partial buffer.
set(SIZE 50331709)

function(fail msg)
  message(FATAL_ERROR "${MODE}: ${msg}")
endfunction()

if(MODE STREQUAL "pipe")
  # gen | ppv -q -s | check  - real pipes on both sides
  execute_process(
    COMMAND "${GEN}" ${SIZE}
    COMMAND "${PPV}" -q -s -B 64K
    COMMAND "${CHECK}" ${SIZE}
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  if(NOT rcs STREQUAL "0;0;0")
    fail("exit codes ${rcs}\n${err}")
  endif()
  if(NOT err MATCHES "\\(${SIZE} bytes\\)")
    fail("statistics missing or wrong:\n${err}")
  endif()
  if(NOT err MATCHES "waiting  input")
    fail("statistics incomplete:\n${err}")
  endif()

elseif(MODE STREQUAL "file")
  # ppv < file > file  - regular files on both sides (no pipe)
  set(in "${WORK}/e2e_in.bin")
  set(out "${WORK}/e2e_out.bin")
  execute_process(COMMAND "${GEN}" ${SIZE} OUTPUT_FILE "${in}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    fail("generator failed")
  endif()
  execute_process(COMMAND "${PPV}" -q INPUT_FILE "${in}" OUTPUT_FILE "${out}"
    RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    fail("ppv exit code ${rc}\n${err}")
  endif()
  execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files "${in}" "${out}" RESULT_VARIABLE rc)
  file(REMOVE "${in}" "${out}")
  if(NOT rc EQUAL 0)
    fail("output differs from input")
  endif()

elseif(MODE STREQUAL "empty")
  execute_process(
    COMMAND "${GEN}" 0
    COMMAND "${PPV}" -q -s
    COMMAND "${CHECK}" 0
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  if(NOT rcs STREQUAL "0;0;0" OR NOT err MATCHES "\\(0 bytes\\)")
    fail("exit codes ${rcs}\n${err}")
  endif()

elseif(MODE STREQUAL "closed")
  # The consumer stops after 1 MiB: ppv must notice and exit with 1.
  execute_process(
    COMMAND "${GEN}" ${SIZE}
    COMMAND "${PPV}" -q
    COMMAND "${CHECK}" 1048576 --stop
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  list(GET rcs 1 ppv_rc)
  if(NOT ppv_rc EQUAL 1 OR NOT err MATCHES "output closed")
    fail("expected 'output closed' and exit code 1, got ${rcs}\n${err}")
  endif()

elseif(MODE STREQUAL "usage")
  execute_process(COMMAND "${PPV}" --bogus RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 2)
    fail("unknown option: expected exit code 2, got ${rc}")
  endif()
  execute_process(COMMAND "${PPV}" -i 0 RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 2)
    fail("bad interval: expected exit code 2, got ${rc}")
  endif()
  execute_process(COMMAND "${PPV}" --version RESULT_VARIABLE rc OUTPUT_VARIABLE out)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "^ppv ")
    fail("--version failed")
  endif()

else()
  fail("unknown MODE")
endif()
