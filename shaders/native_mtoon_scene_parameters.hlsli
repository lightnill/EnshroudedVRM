#ifndef EVRM_NATIVE_MTOON_SCENE_PARAMETERS
#define EVRM_NATIVE_MTOON_SCENE_PARAMETERS
// Exact pinned native light constants:2048 bytes. Numeric layout only;
// caller still admits the current native descriptor, backing, use and lifetime.
struct NativeMToonSceneLightParameters {uint4 words[128];};
#endif
