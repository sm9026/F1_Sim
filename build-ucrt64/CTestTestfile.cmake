# CMake generated Testfile for 
# Source directory: C:/Users/cauch/proj/F1_Sim
# Build directory: C:/Users/cauch/proj/F1_Sim/build-ucrt64
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(f1_sim_dal_tests "C:/Users/cauch/proj/F1_Sim/build-ucrt64/f1_sim_dal_tests.exe" "C:/Users/cauch/proj/F1_Sim/data")
set_tests_properties(f1_sim_dal_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/cauch/proj/F1_Sim/CMakeLists.txt;77;add_test;C:/Users/cauch/proj/F1_Sim/CMakeLists.txt;0;")
add_test(f1_sim_simulation_tests "C:/Users/cauch/proj/F1_Sim/build-ucrt64/f1_sim_simulation_tests.exe" "C:/Users/cauch/proj/F1_Sim/data")
set_tests_properties(f1_sim_simulation_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/cauch/proj/F1_Sim/CMakeLists.txt;82;add_test;C:/Users/cauch/proj/F1_Sim/CMakeLists.txt;0;")
