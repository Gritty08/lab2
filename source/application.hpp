#pragma once
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "graphics_internal.hpp"

namespace application {
    bool initialize();
    void shutdown();
    void update(double time);
    void render(const graphics::internal::FrameData& fd);
}