set(MAJSOUL_SRC
    discard_analyzer.cpp
    replay_json.cpp
    report.cpp
    tenhou_log.cpp
    tile_glyph.cpp)

add_library(majsoul_replay STATIC ${MAJSOUL_SRC})
target_include_directories(majsoul_replay PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(majsoul_replay PUBLIC ${LIB_NAME})

add_executable(analyze_majsoul_paipu analyze_majsoul_paipu.cpp)
target_link_libraries(analyze_majsoul_paipu PRIVATE majsoul_replay)
add_dependencies(analyze_majsoul_paipu ${LIB_NAME})

# The shanten tables and hand decomposition patterns are looked up next to the
# executable, so they travel with it. Copying is its own always-out-of-date target
# rather than a POST_BUILD step, which would not run when only the data changes.
add_custom_target(majsoul_runtime_files ALL
                  COMMAND ${CMAKE_COMMAND} -E copy_directory
                          ${CMAKE_SOURCE_DIR}/data/config/
                          $<TARGET_FILE_DIR:analyze_majsoul_paipu>
                  COMMENT "Copying the shanten tables next to the tool")
add_dependencies(majsoul_runtime_files analyze_majsoul_paipu)

install(TARGETS analyze_majsoul_paipu)
