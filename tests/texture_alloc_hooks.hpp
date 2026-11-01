#pragma once
#include <cstddef>
// Forced into the owned test translation units only; production builds use
// the pinned stb allocator and do not include these fault-injection hooks.
void* texture_fixture_malloc(std::size_t bytes);
void* texture_fixture_realloc(void* pointer,std::size_t bytes);
void texture_fixture_free(void* pointer);
