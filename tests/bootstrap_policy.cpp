#include "../src/standalone_bootstrap_policy.hpp"
#include <iostream>
#include <string>
using namespace enshrouded::vrm;
static int checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "policy failed line " << __LINE__ << '\n'; return 1; } } while (false)
int main() {
    BootstrapPlan plan;
    const auto run=[&](std::wstring_view root, std::wstring_view inherited=L"") { prepare_bootstrap_paths(std::wstring(root)+L"\\vulkan-1.dll",std::wstring(root)+L"\\enshrouded.exe",inherited,false,false,plan); };
    run(L"C:\\game"); CHECK(plan.activate); CHECK(std::wstring_view(plan.paths) == L"C:\\game\\EnshroudedVRM\\Runtime\\enshrouded_vrm_layer.json");
    run(L"C:\\game space\\日本%model",L"C:\\other;D:\\second;"); CHECK(plan.activate); CHECK(std::wstring_view(plan.paths) == L"C:\\other;D:\\second;C:\\game space\\日本%model\\EnshroudedVRM\\Runtime\\enshrouded_vrm_layer.json");
    run(L"C:\\game",L"C:\\GAME\\enshroudedvrm\\runtime\\enshrouded_vrm_layer.json"); CHECK(!plan.activate && plan.reason == BootstrapReason::already_present);
    for (const auto root : {L"relative",L"\\\\server\\share",L"C:\\game;next",L"C:\\game\\..\\other",L"C:\\game\\.",L"C:\\game ",L"C:\\game:stream",L"C:\\game\\\\other"}) { run(root); CHECK(!plan.activate); }
    run(L"C:\\game",std::wstring(32760,L'x')); CHECK(!plan.activate && plan.reason == BootstrapReason::too_long);
    run(std::wstring(L"C:\\")+std::wstring(32748,L'x')); CHECK(!plan.activate && plan.reason == BootstrapReason::too_long);
    run(L"\\\\?\\C:\\game"); CHECK(plan.activate);
    prepare_bootstrap_paths(L"C:\\other\\vulkan-1.dll",L"C:\\game\\enshrouded.exe",L"",false,false,plan); CHECK(!plan.activate && plan.reason == BootstrapReason::wrong_directory);
    prepare_bootstrap_paths(L"C:\\game\\powrprof.dll",L"C:\\game\\enshrouded.exe",L"",false,false,plan); CHECK(!plan.activate && plan.reason == BootstrapReason::wrong_directory);
    std::cout << checks << " bounded policy checks passed\n";
}
