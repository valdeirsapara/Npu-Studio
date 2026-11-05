#pragma once
#include <string>
#include <vector>

namespace hfstudio {

struct DeviceInfo {
    std::string id;         // "NPU", "GPU", "CPU" (as OpenVINO names them)
    std::string full_name;  // "Intel(R) AI Boost", "Intel(R) Arc(TM) 140V GPU (16GB) (iGPU)"
};

// Inference devices OpenVINO can see on this machine, in preference order NPU, GPU, CPU.
std::vector<DeviceInfo> list_devices();

}  // namespace hfstudio
