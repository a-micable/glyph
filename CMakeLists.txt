cmake_minimum_required(VERSION 3.16)
project(glyph_atlas C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)


add_library(glyph_core
    src/header.c
    src/binio.c
    src/glyph_table.c
    src/kerning.c
    src/atlas.c
    src/row_alloc.c
    src/hints.c
    src/reload.c
    src/bitmap.c
    src/cache.c
    src/compress.c
    src/coverage.c
    src/diagnostics.c
    src/filters.c
    src/fontdb.c
    src/layout.c
    src/manifest.c
    src/pack_plan.c
    src/edit.c
    src/metrics.c
    src/optimize.c
    src/render_backend.c
    src/script.c
    src/scene.c
    src/validate.c
    src/tooling.c
)
target_include_directories(glyph_core PUBLIC src)

add_executable(glyph src/main.c)
target_link_libraries(glyph PRIVATE glyph_core)

enable_testing()
add_executable(pack_unpack_test tests/pack_unpack_test.c)
target_link_libraries(pack_unpack_test PRIVATE glyph_core)
add_test(NAME pack_unpack_test COMMAND pack_unpack_test)

add_executable(module_test tests/module_test.c)
target_link_libraries(module_test PRIVATE glyph_core)
add_test(NAME module_test COMMAND module_test)

add_executable(scene_fuzzer_smoke fuzz/scene_fuzzer.c tests/fuzzer_smoke_driver.c)
target_link_libraries(scene_fuzzer_smoke PRIVATE glyph_core)
add_test(NAME scene_fuzzer_smoke COMMAND scene_fuzzer_smoke ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus/scene_fuzzer/basic.scene)

add_executable(generate_fixtures tests/fixtures/generate_fixtures.c)
target_link_libraries(generate_fixtures PRIVATE glyph_core)
add_custom_target(seed_corpus
    COMMAND generate_fixtures
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating .glyph fixtures and fuzz seed corpus"
)
