find_package(Qt6 REQUIRED COMPONENTS Test)

function(magcs_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE magcs_core Qt6::Test)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

magcs_test(tst_protocol)
magcs_test(tst_safetystate)
magcs_test(tst_integration)
magcs_test(tst_network)
magcs_test(tst_messagepath)

# The integration test starts tools/fake_drone/fake_drone.py (needs python3 + `pip install websockets`;
# it is skipped when they are missing). Override the interpreter with MAGCS_PYTHON.
target_compile_definitions(tst_integration PRIVATE
  FAKE_DRONE_PY="${CMAKE_CURRENT_SOURCE_DIR}/../tools/fake_drone/fake_drone.py")
