# End-to-end tests, run with: cmake -DPPV=... -DGEN=... -DCHECK=... -DWORK=... -DMODE=... -P e2e.cmake

# 48 MiB + an odd tail, so the last read is a partial buffer.
set(SIZE 50331709)
set(SIZE_GROUPED "50_331_709") # as the statistics print it (ppv_fmt_group)

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
  if(NOT err MATCHES "\\(${SIZE_GROUPED} bytes\\)")
    fail("statistics missing or wrong:\n${err}")
  endif()
  if(NOT err MATCHES "  time     read .*, write .*: " OR NOT err MATCHES "  io       [0-9]+ transfers")
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

elseif(MODE STREQUAL "live")
  # The live line on stderr (-f: even though stderr is a pipe here): a name
  # prefix, \r between updates, \n after the last one, and no other output.
  # 32 chunks with a 10 ms pause: several updates at -i 0.1, under a second
  # even on Windows, where Sleep() rounds up to the 15 ms timer tick.
  set(LIVE_SIZE 2097152)
  execute_process(
    COMMAND "${GEN}" ${LIVE_SIZE} 10
    COMMAND "${PPV}" -f -i 0.1 -N job
    COMMAND "${CHECK}" ${LIVE_SIZE}
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  if(NOT rcs STREQUAL "0;0;0")
    fail("exit codes ${rcs}\n${err}")
  endif()
  if(NOT err MATCHES "^job\\[")
    fail("live line does not start with the name:\n${err}")
  endif()
  if(NOT err MATCHES "\\]\r" OR NOT err MATCHES "\\]\n$")
    fail("expected \\r separated updates and a final \\n:\n${err}")
  endif()
  string(REGEX MATCHALL "\n" newlines "${err}")
  list(LENGTH newlines n)
  if(NOT n EQUAL 1)
    fail("expected exactly one newline on stderr, got ${n}:\n${err}")
  endif()
  # -q wins over -f: nothing on stderr at all.
  execute_process(
    COMMAND "${GEN}" 100000
    COMMAND "${PPV}" -f -q
    COMMAND "${CHECK}" 100000
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  if(NOT rcs STREQUAL "0;0;0" OR NOT err STREQUAL "")
    fail("-q -f: exit codes ${rcs}, stderr '${err}'")
  endif()

elseif(MODE STREQUAL "wait")
  # -W: no live line while nothing has arrived; the statistics still appear.
  execute_process(
    COMMAND "${GEN}" 0
    COMMAND "${PPV}" -f -W -s -i 0.1
    COMMAND "${CHECK}" 0
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  if(NOT rcs STREQUAL "0;0;0" OR err MATCHES "\\[" OR NOT err MATCHES "\\(0 bytes\\)")
    fail("exit codes ${rcs}\n${err}")
  endif()
  # ... and it appears once data has arrived.
  execute_process(
    COMMAND "${GEN}" 100000
    COMMAND "${PPV}" -f -W
    COMMAND "${CHECK}" 100000
    RESULTS_VARIABLE rcs
    ERROR_VARIABLE err)
  if(NOT rcs STREQUAL "0;0;0" OR NOT err MATCHES "\\[")
    fail("exit codes ${rcs}\n${err}")
  endif()

elseif(MODE STREQUAL "buffer")
  # Smallest and largest transfer buffer.
  foreach(size 4K 256M)
    execute_process(
      COMMAND "${GEN}" ${SIZE}
      COMMAND "${PPV}" -q -s -B ${size}
      COMMAND "${CHECK}" ${SIZE}
      RESULTS_VARIABLE rcs
      ERROR_VARIABLE err)
    if(NOT rcs STREQUAL "0;0;0" OR NOT err MATCHES "\\(${SIZE_GROUPED} bytes\\)")
      fail("-B ${size}: exit codes ${rcs}\n${err}")
    endif()
  endforeach()

elseif(MODE STREQUAL "usage")
  foreach(args "--bogus" "-i;0" "-B;1" "--pid;x" "file" "--;file" "-N")
    execute_process(COMMAND "${PPV}" ${args} RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(NOT rc EQUAL 2 OR NOT err MATCHES "Try 'ppv --help'")
      fail("'${args}': expected exit code 2 and a hint, got ${rc}:\n${err}")
    endif()
  endforeach()
  execute_process(COMMAND "${PPV}" --version RESULT_VARIABLE rc OUTPUT_VARIABLE out)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "^ppv [0-9]+\\.[0-9]+\\.[0-9]+\n$")
    fail("--version failed: ${rc} '${out}'")
  endif()
  execute_process(COMMAND "${PPV}" --help RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0 OR NOT out MATCHES "^Usage:" OR NOT err STREQUAL "")
    fail("--help failed")
  endif()
  # A process that cannot exist: exit 1 with a message, no statistics.
  execute_process(COMMAND "${PPV}" --pid 4000000000 -s RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 1 OR NOT err MATCHES "^ppv: pid 4000000000: ")
    fail("--pid 4000000000: expected exit code 1, got ${rc}:\n${err}")
  endif()

else()
  fail("unknown MODE")
endif()
