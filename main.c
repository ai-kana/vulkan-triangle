#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>

#define WINDOW_HEIGHT   512
#define WINDOW_WIDTH    512

static GLFWwindow* window = NULL;
static VkInstance instance;

static VkDevice logical_device;
static VkQueue graphics_queue;
static VkQueue present_queue;

static VkPhysicalDevice physical_device;
static VkSurfaceKHR surface;

static VkSwapchainKHR swap_chain;
static VkImage* swap_chain_images;
static uint32_t swap_chain_images_count;

static VkImageView* swap_chain_image_views;

static VkFormat swap_chain_format;
static VkExtent2D swap_chain_extent;

static VkRenderPass render_pass;
static VkPipelineLayout pipeline_layout;
static VkPipeline pipeline;

static VkFramebuffer* swap_chain_frame_buffers;

static VkCommandPool command_pool;
static VkCommandBuffer command_buffer;

static VkSemaphore image_available_semaphore;
static VkSemaphore render_finished_semaphore;
static VkFence in_flight_fence;

static const char* device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

void init_window() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

    window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Meow :3", NULL, NULL);
}

uint32_t clamp(uint32_t number, uint32_t min, uint32_t max) {
    if (number < min) {
        return min;
    }

    if (number > max) {
        return max;
    }

    return number;
}

char* read_file(char* path, uint32_t* size) {
    FILE* file = fopen(path, "r");
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    rewind(file);

    while ((*size % 32) != 0) {
        (*size)++;
    }

    char* ret = malloc(*size * sizeof(char));
    fread(ret, sizeof(char), *size, file);

    return ret;
}

struct optional_uint32_t {
    uint32_t value;
    bool assigned;
};

struct queue_family_indices {
    struct optional_uint32_t graphics_family;
    struct optional_uint32_t present_family;
};

struct swap_chain_support_details {
    VkSurfaceCapabilitiesKHR capabilities;

    VkSurfaceFormatKHR* formats;
    uint32_t formats_count;

    VkPresentModeKHR* present_modes;
    uint32_t present_modes_count;
};

struct queue_family_indices find_queue_families(VkPhysicalDevice* device) {
    struct queue_family_indices indices;
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(*device, &family_count, NULL);

    VkQueueFamilyProperties* families = malloc(sizeof(VkQueueFamilyProperties) * family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(*device, &family_count, families);
    for (uint32_t i = 0; i < family_count; i++) {
        VkQueueFamilyProperties family = families[i];
        if (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family.value = i;
            indices.graphics_family.assigned = true;
        }

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(*device, i, surface, &present_support);
        if (present_support) {
            indices.present_family.value = i;
            indices.present_family.assigned = true;
        }
        
        if (indices.graphics_family.assigned && indices.present_family.assigned) {
            break;
        }
    }

    free(families);

    return indices;
}

VkResult create_logical_device() {
    struct queue_family_indices indices = find_queue_families(&physical_device);
    float priority = 1.f;

    uint32_t unique_count = 1;
    if (indices.graphics_family.value != indices.present_family.value) {
        unique_count = 2;
    }

    VkDeviceQueueCreateInfo queue_create_infos[2];
    VkDeviceQueueCreateInfo graphics_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.graphics_family.value,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    VkDeviceQueueCreateInfo present_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = indices.present_family.value,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };

    queue_create_infos[0] = graphics_queue_create_info;
    queue_create_infos[1] = present_queue_create_info;

    VkPhysicalDeviceFeatures features;
    memset(&features, VK_FALSE, sizeof(VkPhysicalDeviceFeatures));

    size_t extension_count = sizeof(device_extensions) / sizeof(char*);
    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queue_create_infos,
        .queueCreateInfoCount = unique_count,
        .pEnabledFeatures = &features,
        .enabledLayerCount = 0,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = device_extensions,
    };

    VkResult out = vkCreateDevice(physical_device, &device_create_info, NULL, &logical_device);
    if (out != VK_SUCCESS) {
        return out;
    }

    vkGetDeviceQueue(logical_device, indices.graphics_family.value, 0, &graphics_queue);
    vkGetDeviceQueue(logical_device, indices.present_family.value, 0, &present_queue);

    return VK_SUCCESS;
}

VkExtent2D choose_swap_chain_extent(VkSurfaceCapabilitiesKHR* capabilities) {
    if (capabilities->currentExtent.width != UINT_MAX) {
        return capabilities->currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D extent = {
        .width = width,
        .height = height,
    };

    extent.width = clamp(extent.width, capabilities->minImageExtent.width, capabilities->maxImageExtent.width);
    extent.height = clamp(extent.height, capabilities->minImageExtent.height, capabilities->maxImageExtent.height);
    return extent;
}

VkPresentModeKHR choose_swap_chain_present_mode(VkPresentModeKHR* modes, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        VkPresentModeKHR mode = modes[i];
        if (mode != VK_PRESENT_MODE_MAILBOX_KHR) {
            continue;
        }

        return mode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkSurfaceFormatKHR choose_swap_chain_surface_format(VkSurfaceFormatKHR* formats, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        VkSurfaceFormatKHR format = formats[i];
        if (format.format != VK_FORMAT_R8G8B8A8_SRGB) {
            continue;
        }

        if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            continue;
        }

        return format;
    }

    return formats[0];
}

struct swap_chain_support_details query_swap_chain_details(VkPhysicalDevice* device) {
    struct swap_chain_support_details details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(*device, surface, &details.capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(*device, surface, &details.formats_count, NULL);
    if (details.formats_count != 0) {
        details.formats = malloc(sizeof(VkSurfaceFormatKHR) * details.formats_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(*device, surface, &details.formats_count, details.formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(*device, surface, &details.present_modes_count, NULL); 
    if (details.present_modes_count != 0) {
        details.present_modes = malloc(sizeof(VkPresentModeKHR) * details.present_modes_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(*device, surface, &details.present_modes_count, details.present_modes);
    }

    return details;
}

VkResult create_swap_chain() {
    struct swap_chain_support_details details = query_swap_chain_details(&physical_device);

    VkSurfaceFormatKHR surface_format = choose_swap_chain_surface_format(details.formats, details.formats_count);
    VkPresentModeKHR present_mode = choose_swap_chain_present_mode(details.present_modes, details.present_modes_count);
    VkExtent2D extent = choose_swap_chain_extent(&details.capabilities);

    uint32_t image_count = details.capabilities.minImageCount + 1;
    if (details.capabilities.maxImageCount > 0 && image_count > details.capabilities.maxImageCount) {
        image_count = details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = details.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    struct queue_family_indices indices = find_queue_families(&physical_device);
    uint32_t family_indices[2] = {
        indices.graphics_family.value, 
        indices.present_family.value
    };

    if (indices.graphics_family.value != indices.present_family.value) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult result = vkCreateSwapchainKHR(logical_device, &create_info, NULL, &swap_chain);
    if (result != VK_SUCCESS) {
        return result;
    }

    vkGetSwapchainImagesKHR(logical_device, swap_chain, &swap_chain_images_count, NULL);
    swap_chain_images = malloc(sizeof(VkImage) * swap_chain_images_count);
    vkGetSwapchainImagesKHR(logical_device, swap_chain, &swap_chain_images_count, swap_chain_images);

    swap_chain_format = surface_format.format;
    swap_chain_extent = extent;

    return result;
}

VkResult create_image_view() {
    swap_chain_image_views = malloc(sizeof(VkImageView) * swap_chain_images_count);
    for (uint32_t i = 0; i < swap_chain_images_count; i++) {
        VkImageViewCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swap_chain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swap_chain_format,
            .components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .components.a = VK_COMPONENT_SWIZZLE_IDENTITY,
            .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1
        };
        VkResult result = vkCreateImageView(logical_device, &create_info, NULL, &swap_chain_image_views[i]);
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
}

VkResult create_render_pass() {
    VkAttachmentDescription color_attachment = {
        .format = swap_chain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference attachment_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pColorAttachments = &attachment_reference,
        .colorAttachmentCount = 1,
    };

    VkSubpassDependency dependancy = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
    };

    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pAttachments = &color_attachment,
        .attachmentCount = 1,
        .pSubpasses = &subpass,
        .subpassCount = 1,
        .pDependencies = &dependancy,
        .dependencyCount = 1,
    };

    return vkCreateRenderPass(logical_device, &render_pass_info, NULL, &render_pass);
}

VkShaderModule create_shader_module(char* binary, uint32_t size) {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pCode = (uint32_t*)binary,
        .codeSize = size 
    };

    VkShaderModule shader_module;
    vkCreateShaderModule(logical_device, &create_info, NULL, &shader_module);
    return shader_module;
}

VkResult create_graphics_pipeline() {
    uint32_t vertex_shader_size;
    uint32_t fragment_shader_size;

    char* vertex_shader_code = read_file("./shaders/vert.spv", &vertex_shader_size);
    char* fragment_shader_code = read_file("./shaders/frag.spv", &fragment_shader_size);

    VkShaderModule vertex_shader = create_shader_module(vertex_shader_code, vertex_shader_size);
    VkShaderModule fragment_shader = create_shader_module(fragment_shader_code, fragment_shader_size);

    VkPipelineShaderStageCreateInfo vertex_shader_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertex_shader,
        .pName = "main"
    };

    VkPipelineShaderStageCreateInfo fragment_shader_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragment_shader,
        .pName = "main"
    };

    VkPipelineShaderStageCreateInfo shader_stages[2] = {
        vertex_shader_create_info,
        fragment_shader_create_info
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthBiasClamp = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    const VkDynamicState dynamic_states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pDynamicStates = dynamic_states,
        .dynamicStateCount = 2,
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pushConstantRangeCount = 0,
    };

    vkCreatePipelineLayout(logical_device, &pipeline_layout_create_info, NULL, &pipeline_layout);

    VkGraphicsPipelineCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pStages = shader_stages,
        .stageCount = 2,
        .pVertexInputState = &vertex_input_create_info,
        .pInputAssemblyState = &input_assembly_create_info,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterizer_create_info,
        .pMultisampleState = &multisampling_create_info,
        .pColorBlendState = &color_blend_create_info,
        .pDynamicState = &dynamic_state_create_info,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
    };

    VkResult result = vkCreateGraphicsPipelines(logical_device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &pipeline);

    vkDestroyShaderModule(logical_device, vertex_shader, NULL);
    vkDestroyShaderModule(logical_device, fragment_shader, NULL);

    free(vertex_shader_code);
    free(fragment_shader_code);

    return result;
}

VkResult create_frame_buffer() {
    swap_chain_frame_buffers = malloc(sizeof(swap_chain_frame_buffers) * swap_chain_images_count);

    for (uint32_t i = 0; i < swap_chain_images_count; i++) {
        VkImageView* image_view = &swap_chain_image_views[i];

        VkFramebufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = render_pass,
            .attachmentCount = 1,
            .pAttachments = image_view,
            .width = swap_chain_extent.width,
            .height = swap_chain_extent.height,
            .layers = 1,
        };

        VkResult result = vkCreateFramebuffer(logical_device, &create_info, NULL, &swap_chain_frame_buffers[i]);
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
}

VkResult create_command_pool() {
    struct queue_family_indices indices = find_queue_families(&physical_device);
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = indices.graphics_family.value,
    };

    return vkCreateCommandPool(logical_device, &create_info, NULL, &command_pool);
}

VkResult create_command_buffer() {
    VkCommandBufferAllocateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    return vkAllocateCommandBuffers(logical_device, &buffer_info, &command_buffer);
}

VkResult record_command_buffer(VkCommandBuffer* buffer, uint32_t image_index) {
    VkCommandBufferBeginInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    vkBeginCommandBuffer(*buffer, &info);

    VkClearValue clear_color = {{{0.f, 0.f, 0.f, 0.1f}}};
    VkRenderPassBeginInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass,
        .framebuffer = swap_chain_frame_buffers[image_index],
        .renderArea.offset = {0, 0},
        .renderArea.extent = swap_chain_extent,
        .clearValueCount = 1,
        .pClearValues = &clear_color
    };

    vkCmdBeginRenderPass(*buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    {
        vkCmdBindPipeline(*buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkViewport viewport = {
            .x = 0.f,
            .y = 0.f,
            .width = swap_chain_extent.width,
            .height = swap_chain_extent.height,
        };
        vkCmdSetViewport(*buffer, 0, 1, &viewport);

        VkRect2D scissors = {
            .offset = {0, 0},
            .extent = swap_chain_extent,
        };
        vkCmdSetScissor(*buffer, 0, 1, &scissors);

        vkCmdDraw(*buffer, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(*buffer);
    return vkEndCommandBuffer(*buffer);
}

VkResult create_sync_objects() {
    VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    VkResult result;
    result = vkCreateSemaphore(logical_device, &semaphore_info, NULL, &image_available_semaphore);
    if (result != VK_SUCCESS) {
        return result;
    }

    result = vkCreateSemaphore(logical_device, &semaphore_info, NULL, &render_finished_semaphore);
    if (result != VK_SUCCESS) {
        return result;
    }

    return vkCreateFence(logical_device, &fence_info, NULL, &in_flight_fence);
}

bool check_extension_support(VkPhysicalDevice* device) {
    size_t count = sizeof(device_extensions) / sizeof(char*);

    uint32_t extension_count = 0;
    vkEnumerateDeviceExtensionProperties(*device, NULL, &extension_count, NULL);

    VkExtensionProperties* available = malloc(extension_count * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(*device, NULL, &extension_count, available);

    uint32_t matches = 0;
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t n = 0; n < extension_count; n++) { 
            if (strcmp(device_extensions[i], available[n].extensionName) == 0) {
                matches++;
                break;
            }
        }
    }

    free(available);

    return matches == count;
}

bool device_suitable(VkPhysicalDevice* device) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(*device, &properties);
    
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceFeatures(*device, &features);
    struct queue_family_indices indices = find_queue_families(device);
    if (!indices.graphics_family.assigned) {
        return false;
    }

    bool supports_extensions = check_extension_support(device);
    bool supports_swap_chain = false;
    if (supports_extensions) {
        struct swap_chain_support_details details = query_swap_chain_details(device);
        supports_swap_chain = details.formats_count != 0 && details.present_modes_count != 0;

        if (details.formats_count > 0) {
            free(details.formats);
        }

        if (details.present_modes_count > 0) {
            free(details.present_modes);
        }
    }

    return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && features.geometryShader && supports_swap_chain;
}

VkResult init_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    if (device_count == 0) {
        return !VK_SUCCESS;
    }

    VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(instance, &device_count, devices);

    physical_device = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < device_count; i++) {
        VkPhysicalDevice device = devices[i];
        if (!device_suitable(&device)) {
            continue;
        }

        physical_device = device;
        break;
    }

    free(devices);

    if (physical_device == VK_NULL_HANDLE) {
        return !VK_SUCCESS;
    }

    return VK_SUCCESS;
}


VkResult create_instance() {
    struct VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Meow",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Meowgine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };

    uint32_t extensions_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensions_count);

    struct VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = extensions_count,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = 0,
    };

    return vkCreateInstance(&create_info, NULL, &instance);
}

VkResult create_surface() {
    return glfwCreateWindowSurface(instance, window, NULL, &surface);
}

VkResult init_vulkan() {
    VkResult result;
    result = create_instance();
    if (result != VK_SUCCESS) {
        puts("Failed to create instance");
        return result;
    }

    result = create_surface();
    if (result != VK_SUCCESS) {
        puts("Failed to create surface");
        return result;
    }

    result = init_device();
    if (result != VK_SUCCESS) {
        puts("Failed to create device");
        return result;
    }

    result = create_logical_device();
    if (result != VK_SUCCESS) {
        puts("Failed to create logical device");
        return result;
    }

    result = create_swap_chain();
    if (result != VK_SUCCESS) {
        puts("Failed to create swap chain");
        return result;
    }

    result = create_image_view();
    if (result != VK_SUCCESS) {
        puts("Failed to create image view");
        return result;
    }

    result = create_render_pass();
    if (result != VK_SUCCESS) {
        puts("Failed to create render pass");
        return result;
    }

    result = create_graphics_pipeline();
    if (result != VK_SUCCESS) {
        puts("Failed to create graphics pipeline");
        return result;
    }

    result = create_frame_buffer();
    if (result != VK_SUCCESS) {
        puts("Failed to create frame buffers");
        return result;
    }

    result = create_command_pool();
    if (result != VK_SUCCESS) {
        puts("Failed to create command pool");
        return result;
    }

    result = create_command_buffer();
    if (result != VK_SUCCESS) {
        puts("Failed to create command buffer");
        return result;
    }

    result = create_sync_objects();
    if (result != VK_SUCCESS) {
        puts("Failed to create sync objects");
        return result;
    }

    return VK_SUCCESS;
}

void draw_frame() {
    vkWaitForFences(logical_device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(logical_device, 1, &in_flight_fence);

    uint32_t image_index;
    vkAcquireNextImageKHR((logical_device), swap_chain, UINT64_MAX, image_available_semaphore, VK_NULL_HANDLE, &image_index);

    vkResetCommandBuffer(command_buffer, 0);
    record_command_buffer(&command_buffer, image_index);

    VkPipelineStageFlags flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pWaitSemaphores = &image_available_semaphore,
        .waitSemaphoreCount = 1,
        .pWaitDstStageMask = &flags,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &render_finished_semaphore,
    };

    vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fence);
    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &render_finished_semaphore,
        .pSwapchains = &swap_chain,
        .swapchainCount = 1,
        .pImageIndices = &image_index,
    };
    vkQueuePresentKHR(present_queue, &present_info);
}

void main_loop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        draw_frame();
    }
}

void cleanup() {
    vkDestroySemaphore(logical_device, image_available_semaphore, NULL);
    vkDestroySemaphore(logical_device, render_finished_semaphore, NULL);

    vkDestroyCommandPool(logical_device, command_pool, NULL);

    for (uint32_t i = 0; i < swap_chain_images_count; i++) {
        vkDestroyFramebuffer(logical_device, swap_chain_frame_buffers[i], NULL);
    }

    vkDestroyPipeline(logical_device, pipeline, NULL);
    vkDestroyPipelineLayout(logical_device, pipeline_layout, NULL);
    vkDestroyRenderPass(logical_device, render_pass, NULL);

    for (uint32_t i = 0; i < swap_chain_images_count; i++) {
        vkDestroyImageView(logical_device, swap_chain_image_views[i], NULL);
    }

    vkDestroySwapchainKHR(logical_device, swap_chain, NULL);
    vkDestroyDevice(logical_device, NULL);

    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);

    free(swap_chain_images);
    free(swap_chain_image_views);

    glfwDestroyWindow(window);
    glfwTerminate();
}

int main() {
    init_window();

    if (init_vulkan() != VK_SUCCESS) {
        return 1;
    }

    main_loop();
    cleanup();

    return 0;
}
