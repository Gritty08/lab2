#include "application.hpp"
#include "graphics_internal.hpp"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <iostream>

namespace application {

namespace {

    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
    };

    struct UniformData {
        glm::vec4 cam_pos;
        glm::vec4 light_dir;
        glm::vec4 light_color;
        glm::vec4 ambient_color;
        glm::vec4 albedo;
        glm::vec4 specular_color;
        glm::vec4 misc;
    };

    const std::vector<Vertex> vertices = {
        {{ 1,  0,  0}, glm::normalize(glm::vec3( 1,  0,  0))},
        {{-1,  0,  0}, glm::normalize(glm::vec3(-1,  0,  0))},
        {{ 0,  1,  0}, glm::normalize(glm::vec3( 0,  1,  0))},
        {{ 0, -1,  0}, glm::normalize(glm::vec3( 0, -1,  0))},
        {{ 0,  0,  1}, glm::normalize(glm::vec3( 0,  0,  1))},
        {{ 0,  0, -1}, glm::normalize(glm::vec3( 0,  0, -1))},
    };

    const std::vector<uint32_t> indices = {
        0, 2, 4,   0, 4, 3,   0, 3, 5,   0, 5, 2,
        1, 4, 2,   1, 3, 4,   1, 5, 3,   1, 2, 5,
    };

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VmaAllocation vertex_alloc = VK_NULL_HANDLE;

    VkBuffer index_buffer = VK_NULL_HANDLE;
    VmaAllocation index_alloc = VK_NULL_HANDLE;

    VkBuffer uniform_buffer = VK_NULL_HANDLE;
    VmaAllocation uniform_alloc = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;

    glm::mat4 model_matrix = glm::mat4(1.0f);
    glm::mat4 view_matrix = glm::mat4(1.0f);
    glm::mat4 proj_matrix = glm::mat4(1.0f);

    bool use_perspective = true;

    float cam_yaw = 30.0f;
    float cam_pitch = 20.0f;
    float cam_distance = 5.0f;
    float cam_fov = 60.0f;

    glm::vec3 light_dir = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
    glm::vec3 light_color = {1.0f, 1.0f, 1.0f};
    float light_intensity = 1.0f;

    float ambient_strength = 0.1f;
    glm::vec3 ambient_color = {1.0f, 1.0f, 1.0f};

    glm::vec3 material_albedo = {0.8f, 0.6f, 0.4f};
    glm::vec3 material_specular = {1.0f, 1.0f, 1.0f};
    float material_shininess = 64.0f;

    std::vector<char> readFile(const std::string& path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("не открылся файл: " + path);
        }
        size_t size = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(size);
        file.seekg(0);
        file.read(buffer.data(), size);
        file.close();
        return buffer;
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module;
        if (vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error("шейдерный модуль не создался");
        }
        return module;
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& out_buffer, VmaAllocation& out_alloc, const void* data = nullptr) {
        VkBufferCreateInfo buf_info{};
        buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.size = size;
        buf_info.usage = usage;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
        alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo info{};
        if (vmaCreateBuffer(graphics::internal::context.allocator, &buf_info, &alloc_info, &out_buffer, &out_alloc, &info) != VK_SUCCESS) {
            throw std::runtime_error("буфер не создался");
        }

        if (data != nullptr) {
            std::memcpy(info.pMappedData, data, size);
        }
    }

    void createDescriptors() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(graphics::internal::context.device, &layout_info, nullptr, &descriptor_layout) != VK_SUCCESS) {
            throw std::runtime_error("descriptor layout не создался");
        }

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;

        if (vkCreateDescriptorPool(graphics::internal::context.device, &pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
            throw std::runtime_error("descriptor pool не создался");
        }

        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &descriptor_layout;

        if (vkAllocateDescriptorSets(graphics::internal::context.device, &alloc_info, &descriptor_set) != VK_SUCCESS) {
            throw std::runtime_error("descriptor set не создался");
        }

        VkDescriptorBufferInfo buffer_info{};
        buffer_info.buffer = uniform_buffer;
        buffer_info.offset = 0;
        buffer_info.range = sizeof(UniformData);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer_info;

        vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
    }

    void createPipeline() {
        auto vert_code = readFile("shaders/octahedron.vert.spv");
        auto frag_code = readFile("shaders/octahedron.frag.spv");
        VkShaderModule vert_module = createShaderModule(vert_code);
        VkShaderModule frag_module = createShaderModule(frag_code);

        VkPipelineShaderStageCreateInfo vert_stage{};
        vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vert_stage.module = vert_module;
        vert_stage.pName = "main";

        VkPipelineShaderStageCreateInfo frag_stage{};
        frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        frag_stage.module = frag_module;
        frag_stage.pName = "main";

        VkPipelineShaderStageCreateInfo stages[] = {vert_stage, frag_stage};

        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, pos);
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, normal);

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 2;
        vertex_input.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil{};
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo color_blend{};
        color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.attachmentCount = 1;
        color_blend.pAttachments = &blend_attachment;

        VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(glm::mat4) * 3;

        VkPipelineLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.setLayoutCount = 1;
        layout_info.pSetLayouts = &descriptor_layout;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;

        if (vkCreatePipelineLayout(graphics::internal::context.device, &layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
            throw std::runtime_error("pipeline layout не создался");
        }

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &raster;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic;
        pipeline_info.layout = pipeline_layout;
        pipeline_info.renderPass = graphics::internal::context.render_pass;
        pipeline_info.subpass = 0;

        if (vkCreateGraphicsPipelines(graphics::internal::context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
            throw std::runtime_error("pipeline не создался");
        }

        vkDestroyShaderModule(graphics::internal::context.device, vert_module, nullptr);
        vkDestroyShaderModule(graphics::internal::context.device, frag_module, nullptr);
    }

}

bool initialize() {
    try {
        createBuffer(vertices.size() * sizeof(Vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            vertex_buffer, vertex_alloc,
            vertices.data()
        );

        createBuffer(indices.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            index_buffer, index_alloc,
            indices.data()
        );

        createBuffer(sizeof(UniformData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            uniform_buffer, uniform_alloc,
            nullptr
        );

        createDescriptors();
        createPipeline();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
    return true;
}

void shutdown() {
    auto& ctx = graphics::internal::context;
    vkDeviceWaitIdle(ctx.device);

    vkDestroyDescriptorPool(ctx.device, descriptor_pool, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, descriptor_layout, nullptr);

    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(ctx.device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(ctx.device, pipeline_layout, nullptr);
        pipeline_layout = VK_NULL_HANDLE;
    }
    if (uniform_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator, uniform_buffer, uniform_alloc);
        uniform_buffer = VK_NULL_HANDLE;
    }
    if (vertex_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator, vertex_buffer, vertex_alloc);
        vertex_buffer = VK_NULL_HANDLE;
    }
    if (index_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(ctx.allocator, index_buffer, index_alloc);
        index_buffer = VK_NULL_HANDLE;
    }
}

void update(double time) {
    float yaw_rad = glm::radians(cam_yaw);
    float pitch_rad = glm::radians(cam_pitch);
    glm::vec3 cam_pos(
        cam_distance * std::cos(pitch_rad) * std::sin(yaw_rad),
        cam_distance * std::sin(pitch_rad),
        cam_distance * std::cos(pitch_rad) * std::cos(yaw_rad)
    );
    view_matrix = glm::lookAt(cam_pos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    auto& ctx = graphics::internal::context;
    float aspect = static_cast<float>(ctx.swapchain_extent.width) / static_cast<float>(ctx.swapchain_extent.height);

    if (use_perspective) {
        proj_matrix = glm::perspective(glm::radians(cam_fov), aspect, 0.1f, 100.0f);
    } else {
        float h = 3.0f;
        float w = h * aspect;
        proj_matrix = glm::ortho(-w, w, -h, h, -100.0f, 100.0f);
    }

    model_matrix = glm::rotate(glm::mat4(1.0f), static_cast<float>(time) * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));

    UniformData data{};
    data.cam_pos = glm::vec4(cam_pos, 1.0f);
    data.light_dir = glm::vec4(glm::normalize(light_dir), light_intensity);
    data.light_color = glm::vec4(light_color, 1.0f);
    data.ambient_color = glm::vec4(ambient_color, 1.0f);
    data.albedo = glm::vec4(material_albedo, 1.0f);
    data.specular_color = glm::vec4(material_specular, 1.0f);
    data.misc = glm::vec4(material_shininess, ambient_strength, 0.0f, 0.0f);

    VmaAllocationInfo alloc_info{};
    vmaGetAllocationInfo(ctx.allocator, uniform_alloc, &alloc_info);
    std::memcpy(alloc_info.pMappedData, &data, sizeof(UniformData));

    ImGui::Begin("Octahedron");

    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Checkbox("Perspective projection", &use_perspective);

    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::SliderFloat("Yaw", &cam_yaw, -180.0f, 180.0f);
    ImGui::SliderFloat("Pitch", &cam_pitch, -89.0f, 89.0f);
    ImGui::SliderFloat("Distance", &cam_distance, 2.0f, 15.0f);
    ImGui::SliderFloat("FOV", &cam_fov, 20.0f, 120.0f);

    ImGui::Separator();
    ImGui::Text("Directional light");
    ImGui::SliderFloat3("Direction", &light_dir.x, -1.0f, 1.0f);
    ImGui::ColorEdit3("Light color", &light_color.x);
    ImGui::SliderFloat("Intensity", &light_intensity, 0.0f, 5.0f);

    ImGui::Separator();
    ImGui::Text("Ambient");
    ImGui::SliderFloat("Ambient strength", &ambient_strength, 0.0f, 1.0f);
    ImGui::ColorEdit3("Ambient color", &ambient_color.x);

    ImGui::Separator();
    ImGui::Text("Material");
    ImGui::ColorEdit3("Albedo", &material_albedo.x);
    ImGui::ColorEdit3("Specular color", &material_specular.x);
    ImGui::SliderFloat("Shininess", &material_shininess, 1.0f, 256.0f);

    ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    VkClearValue clear_values[2]{};
    clear_values[0].color = {{0.05f, 0.05f, 0.08f, 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = ctx.render_pass;
    render_pass_info.framebuffer = fd.framebuffer;
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = ctx.swapchain_extent;
    render_pass_info.clearValueCount = 2;
    render_pass_info.pClearValues = clear_values;

    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(ctx.swapchain_extent.width);
    viewport.height = static_cast<float>(ctx.swapchain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = ctx.swapchain_extent;
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT32);

    struct PushData {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    }
    push_data{model_matrix, view_matrix, proj_matrix};

    vkCmdPushConstants(fd.command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData), &push_data);

    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

}