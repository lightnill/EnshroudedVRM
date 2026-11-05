#include "standalone_bootstrap_policy.hpp"
#include <iostream>
#include <string_view>

int main() {
    using namespace enshrouded::vrm;
    static BootstrapPlan plan;
    prepare_bootstrap_paths(L"C:\\Game\\dinput8.dll", L"C:\\Game\\enshrouded.exe", L"", false, false, plan);
    if (!plan.activate || plan.reason != BootstrapReason::ready ||
        std::wstring_view(plan.manifest) != L"C:\\Game\\EnshroudedVRM\\Runtime\\enshrouded_vrm_layer.json") {
        std::cerr << "dinput8 bootstrap must prepare the checked colocated layer\n";
        return 1;
    }
    prepare_bootstrap_paths(L"C:\\Other\\dinput8.dll", L"C:\\Game\\enshrouded.exe", L"", false, false, plan);
    if (plan.activate || plan.reason != BootstrapReason::wrong_directory) return 2;
    prepare_bootstrap_paths(L"C:\\Game\\dinput8.dll", L"C:\\Game\\other.exe", L"", false, false, plan);
    if (plan.activate || plan.reason != BootstrapReason::wrong_process) return 3;
    prepare_bootstrap_paths(L"C:\\Game\\dinput8.dll", L"C:\\Game\\enshrouded.exe", L"", true, false, plan);
    if (plan.activate || plan.reason != BootstrapReason::disabled) return 4;
    prepare_bootstrap_paths(L"C:\\Game\\dinput8.dll", L"C:\\Game\\enshrouded.exe", L"", false, true, plan);
    if (plan.activate || plan.reason != BootstrapReason::search_override) return 5;
    prepare_bootstrap_paths(L"C:\\Game\\dinput8.dll", L"C:\\Game\\enshrouded.exe",
        L"C:\\Game\\EnshroudedVRM\\Runtime\\enshrouded_vrm_layer.json", false, false, plan);
    if (plan.activate || plan.reason != BootstrapReason::already_present) return 6;
    std::cout << "input owner, layer path, disable and duplicate policies passed\n";
}
