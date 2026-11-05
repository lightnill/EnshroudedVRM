#pragma once

#if defined(_MSC_VER)
#include <intrin.h>
#define EVRM_FIXTURE_NOINLINE __declspec(noinline)
#define EVRM_FIXTURE_RETURN_ADDRESS() _ReturnAddress()
#else
#define EVRM_FIXTURE_NOINLINE __attribute__((noinline))
#define EVRM_FIXTURE_RETURN_ADDRESS() __builtin_extract_return_addr(__builtin_return_address(0))
#endif
