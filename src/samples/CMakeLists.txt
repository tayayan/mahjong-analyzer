file(GLOB ENTRY_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
set(SAMPLE_TARGETS "")

foreach(ENTRY_FILE ${ENTRY_FILES})
  get_filename_component(BASE_NAME ${ENTRY_FILE} NAME_WE)

  add_executable(${BASE_NAME} ${ENTRY_FILE})
  if (MSVC)
    target_link_libraries(${BASE_NAME} ${LIB_NAME} ${CMAKE_DL_LIBS}
                          Boost::filesystem Boost::system spdlog)
  elseif(ENABLE_OPENMP)
    target_link_libraries(${BASE_NAME} ${LIB_NAME} ${CMAKE_DL_LIBS}
                          Boost::filesystem Boost::system spdlog::spdlog
                          OpenMP::OpenMP_CXX)
  else()
    target_link_libraries(${BASE_NAME} ${LIB_NAME} ${CMAKE_DL_LIBS}
                          Boost::filesystem Boost::system spdlog::spdlog
                          -static-libgcc -static-libstdc++ -static)
  endif()
  add_dependencies(${BASE_NAME} ${LIB_NAME})
  list(APPEND SAMPLE_TARGETS ${BASE_NAME})
  install(TARGETS ${BASE_NAME})
endforeach(ENTRY_FILE ${ENTRY_FILES})

# Every sample runs from the same directory, so the shanten tables are copied there once
# for all of them. Attaching the copy to each target as a POST_BUILD step instead starts
# several copies of the same files into the same directory in parallel, which fails
# intermittently ("Error copying directory ... Permission denied").
if(SAMPLE_TARGETS)
  list(GET SAMPLE_TARGETS 0 SAMPLE_OUTPUT_ANCHOR)
  add_custom_target(sample_config_files ALL
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            ${CMAKE_SOURCE_DIR}/data/config/
                            $<TARGET_FILE_DIR:${SAMPLE_OUTPUT_ANCHOR}>
                    COMMENT "Copying the shanten tables next to the samples")
  add_dependencies(sample_config_files ${SAMPLE_TARGETS})
endif()
