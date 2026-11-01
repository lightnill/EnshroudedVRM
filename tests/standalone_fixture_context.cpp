// Explicit test-only admission for existing owned component fixtures. This is
// never compiled into the runtime/player package or selected by environment.
#include "../src/standalone_startup.hpp"
namespace evrm {
const RuntimeStartupContext& runtime_startup_context()noexcept {
    static const RuntimeStartupContext value=[] {RuntimeStartupContext context;context.status=RuntimeStartupStatus::Ready;context.state_writable=true;return context;}();
    return value;
}
}
