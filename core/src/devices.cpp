#include "hfstudio/devices.hpp"

#include <openvino/openvino.hpp>

#include <algorithm>

namespace hfstudio {

std::vector<DeviceInfo> list_devices() {
    ov::Core core;
    std::vector<DeviceInfo> out;
    for (const auto& id : core.get_available_devices()) {
        DeviceInfo d;
        d.id = id;
        try {
            d.full_name = core.get_property(id, ov::device::full_name);
        } catch (const std::exception&) {
            d.full_name = id;
        }
        out.push_back(std::move(d));
    }
    auto rank = [](const std::string& id) {
        if (id.rfind("NPU", 0) == 0) return 0;
        if (id.rfind("GPU", 0) == 0) return 1;
        return 2;
    };
    std::stable_sort(out.begin(), out.end(), [&](const DeviceInfo& a, const DeviceInfo& b) { return rank(a.id) < rank(b.id); });
    return out;
}

}  // namespace hfstudio
