/*
 * Goosy — Vulkan Hardware-Accelerated Rendering Backend
 * Native Wayland GPU rendering using VK_KHR_wayland_surface, precompiled SPIR-V,
 * host-coherent vertex streaming, and hardware texture atlas.
 */

#define _POSIX_C_SOURCE 200809L

#include "goosy_render.h"
#include "goosy_vk_shaders.h"
#include "goosy_image.h"
#include "goosy_icon_data.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VK_QUADS 131072
#define VK_ATLAS_SIZE 2048
#define MAX_VK_ATLAS_SLOTS 8192

static uint32_t g_vk_bg_rgb = 0;

typedef struct {
    float x, y;
    float u, v;
    float r, g, b, a;
} vk_vertex_t;

typedef struct {
    int32_t  used;
    uint32_t codepoint;
    uint8_t  style;
    float    u0, v0, u1, v1;
    int32_t  bearing_x;
    int32_t  bearing_y;
    int32_t  width;
    int32_t  height;
} vk_atlas_slot_t;

typedef struct {
    float u_viewport[2];
} vk_push_const_t;

static VkInstance       g_vk_instance       = VK_NULL_HANDLE;
static VkSurfaceKHR     g_vk_surface        = VK_NULL_HANDLE;
static VkPhysicalDevice g_vk_gpu            = VK_NULL_HANDLE;
static VkDevice         g_vk_device         = VK_NULL_HANDLE;
static VkQueue          g_vk_queue          = VK_NULL_HANDLE;
static uint32_t         g_vk_queue_family   = 0;

#define MAX_VK_IMAGES 16

static VkSwapchainKHR   g_vk_swapchain      = VK_NULL_HANDLE;
static VkFormat         g_vk_format         = VK_FORMAT_B8G8R8A8_UNORM;
static VkExtent2D       g_vk_extent         = { 800, 600 };
static uint32_t         g_vk_image_count    = 0;
static VkImage          g_vk_images[MAX_VK_IMAGES];
static VkImageView      g_vk_views[MAX_VK_IMAGES];
static VkFramebuffer    g_vk_framebuffers[MAX_VK_IMAGES];

static VkRenderPass     g_vk_render_pass    = VK_NULL_HANDLE;
static VkPipelineLayout g_vk_pipe_layout    = VK_NULL_HANDLE;
static VkPipeline       g_vk_pipeline       = VK_NULL_HANDLE;

static VkCommandPool    g_vk_cmd_pool       = VK_NULL_HANDLE;
static VkCommandBuffer  g_vk_cmd_bufs[MAX_VK_IMAGES];
static VkSemaphore      g_vk_sem_available  = VK_NULL_HANDLE;
static VkSemaphore      g_vk_sem_rendered   = VK_NULL_HANDLE;
static VkFence          g_vk_fence          = VK_NULL_HANDLE;

static VkBuffer         g_vk_vbo            = VK_NULL_HANDLE;
static VkDeviceMemory   g_vk_vbo_mem        = VK_NULL_HANDLE;
static vk_vertex_t     *g_vk_mapped_vbo     = NULL;

static VkBuffer         g_vk_ebo            = VK_NULL_HANDLE;
static VkDeviceMemory   g_vk_ebo_mem        = VK_NULL_HANDLE;

static VkImage          g_vk_atlas_img      = VK_NULL_HANDLE;
static VkDeviceMemory   g_vk_atlas_mem      = VK_NULL_HANDLE;
static VkImageView      g_vk_atlas_view     = VK_NULL_HANDLE;
static VkSampler        g_vk_atlas_sampler  = VK_NULL_HANDLE;
static VkDescriptorSetLayout g_vk_desc_layout = VK_NULL_HANDLE;
static VkDescriptorPool g_vk_desc_pool      = VK_NULL_HANDLE;
static VkDescriptorSet  g_vk_desc_set       = VK_NULL_HANDLE;

static VkBuffer         g_vk_staging_buf    = VK_NULL_HANDLE;
static VkDeviceMemory   g_vk_staging_mem    = VK_NULL_HANDLE;
static uint8_t         *g_vk_staging_ptr    = NULL;

static int32_t g_cur_w     = 800;
static int32_t g_cur_h     = 600;
static int32_t g_cur_scale = 1;

static int32_t g_quad_count = 0;
static uint32_t g_active_image_idx = 0;

static vk_atlas_slot_t g_atlas_slots[MAX_VK_ATLAS_SLOTS];
static int32_t         g_atlas_slot_count = 0;
static int32_t         g_atlas_cur_x = 1;
static int32_t         g_atlas_cur_y = 1;
static int32_t         g_atlas_row_h = 0;

static uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(g_vk_gpu, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return 0;
}

static VkShaderModule create_shader_module(const uint32_t *code, size_t size_bytes) {
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size_bytes,
        .pCode = code
    };
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g_vk_device, &ci, NULL, &mod) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return mod;
}

static bool create_swapchain_and_views(void) {
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_vk_gpu, g_vk_surface, &caps) != VK_SUCCESS) {
        return false;
    }

    uint32_t req_w = (uint32_t)(g_cur_w * g_cur_scale);
    uint32_t req_h = (uint32_t)(g_cur_h * g_cur_scale);

    if (caps.currentExtent.width != 0xFFFFFFFFU && caps.currentExtent.width > 0) {
        g_vk_extent.width = caps.currentExtent.width;
    } else {
        if (req_w < caps.minImageExtent.width) req_w = caps.minImageExtent.width;
        if (req_w > caps.maxImageExtent.width) req_w = caps.maxImageExtent.width;
        g_vk_extent.width = req_w;
    }

    if (caps.currentExtent.height != 0xFFFFFFFFU && caps.currentExtent.height > 0) {
        g_vk_extent.height = caps.currentExtent.height;
    } else {
        if (req_h < caps.minImageExtent.height) req_h = caps.minImageExtent.height;
        if (req_h > caps.maxImageExtent.height) req_h = caps.maxImageExtent.height;
        g_vk_extent.height = req_h;
    }

    uint32_t desired_images = caps.minImageCount + 1;
    if (desired_images < caps.minImageCount) desired_images = caps.minImageCount;
    if (caps.maxImageCount > 0 && desired_images > caps.maxImageCount) {
        desired_images = caps.maxImageCount;
    }
    if (desired_images > MAX_VK_IMAGES) {
        desired_images = MAX_VK_IMAGES;
    }

    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk_gpu, g_vk_surface, &mode_count, NULL);
    if (mode_count > 0) {
        VkPresentModeKHR modes[16];
        if (mode_count > 16) mode_count = 16;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_vk_gpu, g_vk_surface, &mode_count, modes);
        for (uint32_t m = 0; m < mode_count; m++) {
            if (modes[m] == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }
    }

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            composite_alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        } else {
            composite_alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
    }

    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_vk_surface,
        .minImageCount = desired_images,
        .imageFormat = g_vk_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = g_vk_extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = composite_alpha,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    if (vkCreateSwapchainKHR(g_vk_device, &sci, NULL, &g_vk_swapchain) != VK_SUCCESS) {
        return false;
    }

    uint32_t actual_images = 0;
    vkGetSwapchainImagesKHR(g_vk_device, g_vk_swapchain, &actual_images, NULL);
    if (actual_images > MAX_VK_IMAGES) {
        actual_images = MAX_VK_IMAGES;
    }
    g_vk_image_count = actual_images;
    vkGetSwapchainImagesKHR(g_vk_device, g_vk_swapchain, &g_vk_image_count, g_vk_images);

    for (uint32_t i = 0; i < g_vk_image_count; i++) {
        VkImageViewCreateInfo ivci = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = g_vk_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = g_vk_format,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0, .levelCount = 1,
                .baseArrayLayer = 0, .layerCount = 1
            }
        };
        if (vkCreateImageView(g_vk_device, &ivci, NULL, &g_vk_views[i]) != VK_SUCCESS) {
            return false;
        }

        VkFramebufferCreateInfo fbci = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = g_vk_render_pass,
            .attachmentCount = 1,
            .pAttachments = &g_vk_views[i],
            .width = g_vk_extent.width,
            .height = g_vk_extent.height,
            .layers = 1
        };
        if (vkCreateFramebuffer(g_vk_device, &fbci, NULL, &g_vk_framebuffers[i]) != VK_SUCCESS) {
            return false;
        }
    }
    return true;
}

static bool vk_init(void) {
    struct wl_display *wl_dpy = goosy_wl_get_display();
    struct wl_surface *wl_surf = goosy_wl_get_surface();
    if (!wl_dpy || !wl_surf) return false;

    const char *exts[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface"
    };

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Goosy",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Goosy Core",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1
    };

    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = exts
    };

    if (vkCreateInstance(&ici, NULL, &g_vk_instance) != VK_SUCCESS) {
        return false;
    }

    VkWaylandSurfaceCreateInfoKHR wsci = {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = wl_dpy,
        .surface = wl_surf
    };

    if (vkCreateWaylandSurfaceKHR(g_vk_instance, &wsci, NULL, &g_vk_surface) != VK_SUCCESS) {
        return false;
    }

    /* Choose Physical Device */
    uint32_t gpu_count = 0;
    vkEnumeratePhysicalDevices(g_vk_instance, &gpu_count, NULL);
    if (gpu_count == 0) return false;

    VkPhysicalDevice gpus[16];
    if (gpu_count > 16) gpu_count = 16;
    vkEnumeratePhysicalDevices(g_vk_instance, &gpu_count, gpus);

    g_vk_gpu = gpus[0];
    for (uint32_t i = 0; i < gpu_count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            g_vk_gpu = gpus[i];
            break;
        }
    }

    /* Find Graphics & Present Queue Family */
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk_gpu, &qf_count, NULL);
    VkQueueFamilyProperties qfs[16];
    if (qf_count > 16) qf_count = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk_gpu, &qf_count, qfs);

    bool qf_found = false;
    for (uint32_t i = 0; i < qf_count; i++) {
        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_vk_gpu, i, g_vk_surface, &present_support);
        if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support) {
            g_vk_queue_family = i;
            qf_found = true;
            break;
        }
    }
    if (!qf_found) return false;

    /* Create Logical Device */
    float q_priority = 1.0f;
    VkDeviceQueueCreateInfo dqci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = g_vk_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &q_priority
    };

    const char *dev_exts[] = { "VK_KHR_swapchain" };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &dqci,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = dev_exts
    };

    if (vkCreateDevice(g_vk_gpu, &dci, NULL, &g_vk_device) != VK_SUCCESS) {
        return false;
    }
    vkGetDeviceQueue(g_vk_device, g_vk_queue_family, 0, &g_vk_queue);

    /* Render Pass */
    VkAttachmentDescription color_att = {
        .format = g_vk_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref
    };

    VkRenderPassCreateInfo rpci = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_att,
        .subpassCount = 1,
        .pSubpasses = &subpass
    };
    if (vkCreateRenderPass(g_vk_device, &rpci, NULL, &g_vk_render_pass) != VK_SUCCESS) {
        return false;
    }

    g_cur_w = goosy_wl_width();
    g_cur_h = goosy_wl_height();
    g_cur_scale = goosy_wl_scale();
    if (!create_swapchain_and_views()) return false;

    /* Descriptor Set Layout & Pipeline Layout */
    VkDescriptorSetLayoutBinding dslb = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo dslci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &dslb
    };
    if (vkCreateDescriptorSetLayout(g_vk_device, &dslci, NULL, &g_vk_desc_layout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(vk_push_const_t)
    };
    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &g_vk_desc_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr
    };
    if (vkCreatePipelineLayout(g_vk_device, &plci, NULL, &g_vk_pipe_layout) != VK_SUCCESS) {
        return false;
    }

    /* Compile Embedded Shaders into Modules */
    VkShaderModule vs_mod = create_shader_module(g_vk_vert_spv, sizeof(g_vk_vert_spv));
    VkShaderModule fs_mod = create_shader_module(g_vk_frag_spv, sizeof(g_vk_frag_spv));
    if (!vs_mod || !fs_mod) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs_mod,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs_mod,
            .pName = "main"
        }
    };

    /* Vertex Input Format */
    VkVertexInputBindingDescription vibd = {
        .binding = 0,
        .stride = sizeof(vk_vertex_t),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    VkVertexInputAttributeDescription viad[3] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,       .offset = offsetof(vk_vertex_t, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,       .offset = offsetof(vk_vertex_t, u) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(vk_vertex_t, r) }
    };

    VkPipelineVertexInputStateCreateInfo pvisi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vibd,
        .vertexAttributeDescriptionCount = 3,
        .pVertexAttributeDescriptions = viad
    };

    VkPipelineInputAssemblyStateCreateInfo piasi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkViewport vp = { 0, 0, (float)g_vk_extent.width, (float)g_vk_extent.height, 0.0f, 1.0f };
    VkRect2D sc = { {0, 0}, g_vk_extent };
    VkPipelineViewportStateCreateInfo pvsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp,
        .scissorCount = 1, .pScissors = &sc
    };

    VkPipelineRasterizationStateCreateInfo prsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo pmssi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineColorBlendAttachmentState cbas = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo pcbsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &cbas
    };

    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo pdsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states
    };

    VkGraphicsPipelineCreateInfo gpci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &pvisi,
        .pInputAssemblyState = &piasi,
        .pViewportState = &pvsi,
        .pRasterizationState = &prsi,
        .pMultisampleState = &pmssi,
        .pColorBlendState = &pcbsi,
        .pDynamicState = &pdsi,
        .layout = g_vk_pipe_layout,
        .renderPass = g_vk_render_pass,
        .subpass = 0
    };

    if (vkCreateGraphicsPipelines(g_vk_device, VK_NULL_HANDLE, 1, &gpci, NULL, &g_vk_pipeline) != VK_SUCCESS) {
        return false;
    }
    vkDestroyShaderModule(g_vk_device, vs_mod, NULL);
    vkDestroyShaderModule(g_vk_device, fs_mod, NULL);

    /* Allocate Host-Coherent Vertex Buffer */
    VkDeviceSize vbo_size = sizeof(vk_vertex_t) * MAX_VK_QUADS * 4;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vbo_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    vkCreateBuffer(g_vk_device, &bci, NULL, &g_vk_vbo);

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(g_vk_device, g_vk_vbo, &mr);
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
    };
    vkAllocateMemory(g_vk_device, &mai, NULL, &g_vk_vbo_mem);
    vkBindBufferMemory(g_vk_device, g_vk_vbo, g_vk_vbo_mem, 0);
    vkMapMemory(g_vk_device, g_vk_vbo_mem, 0, vbo_size, 0, (void **)&g_vk_mapped_vbo);

    /* Allocate Static Index Buffer */
    VkDeviceSize ebo_size = sizeof(uint32_t) * MAX_VK_QUADS * 6;
    bci.size = ebo_size;
    bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    vkCreateBuffer(g_vk_device, &bci, NULL, &g_vk_ebo);
    vkGetBufferMemoryRequirements(g_vk_device, g_vk_ebo, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(g_vk_device, &mai, NULL, &g_vk_ebo_mem);
    vkBindBufferMemory(g_vk_device, g_vk_ebo, g_vk_ebo_mem, 0);

    uint32_t *indices = NULL;
    vkMapMemory(g_vk_device, g_vk_ebo_mem, 0, ebo_size, 0, (void **)&indices);
    if (indices) {
        for (uint32_t i = 0; i < MAX_VK_QUADS; i++) {
            uint32_t base = i * 4;
            indices[i * 6 + 0] = base + 0;
            indices[i * 6 + 1] = base + 1;
            indices[i * 6 + 2] = base + 2;
            indices[i * 6 + 3] = base + 2;
            indices[i * 6 + 4] = base + 3;
            indices[i * 6 + 5] = base + 0;
        }
        vkUnmapMemory(g_vk_device, g_vk_ebo_mem);
    }

    /* Staging Buffer for Texture Atlas Uploads */
    bci.size = VK_ATLAS_SIZE * VK_ATLAS_SIZE;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(g_vk_device, &bci, NULL, &g_vk_staging_buf);
    vkGetBufferMemoryRequirements(g_vk_device, g_vk_staging_buf, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(g_vk_device, &mai, NULL, &g_vk_staging_mem);
    vkBindBufferMemory(g_vk_device, g_vk_staging_buf, g_vk_staging_mem, 0);
    vkMapMemory(g_vk_device, g_vk_staging_mem, 0, bci.size, 0, (void **)&g_vk_staging_ptr);

    /* Create Texture Atlas Image */
    VkImageCreateInfo ici_atlas = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .extent = { VK_ATLAS_SIZE, VK_ATLAS_SIZE, 1 },
        .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    vkCreateImage(g_vk_device, &ici_atlas, NULL, &g_vk_atlas_img);
    vkGetImageMemoryRequirements(g_vk_device, g_vk_atlas_img, &mr);
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_vk_device, &mai, NULL, &g_vk_atlas_mem);
    vkBindImageMemory(g_vk_device, g_vk_atlas_img, g_vk_atlas_mem, 0);

    VkImageViewCreateInfo ivci_atlas = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = g_vk_atlas_img,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    vkCreateImageView(g_vk_device, &ivci_atlas, NULL, &g_vk_atlas_view);

    VkSamplerCreateInfo sci_atlas = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
    };
    vkCreateSampler(g_vk_device, &sci_atlas, NULL, &g_vk_atlas_sampler);

    /* Descriptor Pool and Set */
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size
    };
    vkCreateDescriptorPool(g_vk_device, &dpci, NULL, &g_vk_desc_pool);

    VkDescriptorSetAllocateInfo dsai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = g_vk_desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &g_vk_desc_layout
    };
    vkAllocateDescriptorSets(g_vk_device, &dsai, &g_vk_desc_set);

    VkDescriptorImageInfo dii = {
        .sampler = g_vk_atlas_sampler,
        .imageView = g_vk_atlas_view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet wds = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = g_vk_desc_set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &dii
    };
    vkUpdateDescriptorSets(g_vk_device, 1, &wds, 0, NULL);

    /* Command Pool & Synchronization Primitives */
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = g_vk_queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };
    vkCreateCommandPool(g_vk_device, &cpci, NULL, &g_vk_cmd_pool);

    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_vk_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_VK_IMAGES
    };
    vkAllocateCommandBuffers(g_vk_device, &cbai, g_vk_cmd_bufs);

    VkSemaphoreCreateInfo sci_sem = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    vkCreateSemaphore(g_vk_device, &sci_sem, NULL, &g_vk_sem_available);
    vkCreateSemaphore(g_vk_device, &sci_sem, NULL, &g_vk_sem_rendered);
    vkCreateFence(g_vk_device, &fci, NULL, &g_vk_fence);

    return true;
}

static void vk_push_quad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         uint32_t argb) {
    if (g_quad_count >= MAX_VK_QUADS || !g_vk_mapped_vbo) return;
    float a = ((argb >> 24) & 0xFF) / 255.0f;
    float r = ((argb >> 16) & 0xFF) / 255.0f;
    float g = ((argb >> 8)  & 0xFF) / 255.0f;
    float b = (argb & 0xFF)         / 255.0f;

    int32_t idx = g_quad_count * 4;
    g_vk_mapped_vbo[idx + 0] = (vk_vertex_t){ x,     y,     u0, v0, r, g, b, a };
    g_vk_mapped_vbo[idx + 1] = (vk_vertex_t){ x + w, y,     u1, v0, r, g, b, a };
    g_vk_mapped_vbo[idx + 2] = (vk_vertex_t){ x + w, y + h, u1, v1, r, g, b, a };
    g_vk_mapped_vbo[idx + 3] = (vk_vertex_t){ x,     y + h, u0, v1, r, g, b, a };
    g_quad_count++;
}

static void vk_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgb) {
    if (w <= 0 || h <= 0) return;
    uint32_t argb = 0xFF000000U | (rgb & 0x00FFFFFFU);
    vk_push_quad((float)x, (float)y, (float)w, (float)h, -1.0f, -1.0f, -1.0f, -1.0f, argb);
}

/* Upload glyph to Vulkan Texture Atlas via Staging Buffer */
static vk_atlas_slot_t *vk_get_or_upload_glyph(uint32_t cp, uint8_t style) {
    for (int32_t i = 0; i < g_atlas_slot_count; i++) {
        if (g_atlas_slots[i].used && g_atlas_slots[i].codepoint == cp && g_atlas_slots[i].style == style) {
            return &g_atlas_slots[i];
        }
    }

    const goosy_glyph_t *g = goosy_font_get_glyph(cp, style);
    if (!g || !g->bitmap || g->width <= 0 || g->height <= 0) return NULL;

    if (g_atlas_cur_x + g->width + 1 >= VK_ATLAS_SIZE) {
        g_atlas_cur_x = 1;
        g_atlas_cur_y += g_atlas_row_h + 1;
        g_atlas_row_h = 0;
    }
    if (g_atlas_cur_y + g->height + 1 >= VK_ATLAS_SIZE) {
        g_atlas_slot_count = 0;
        g_atlas_cur_x = 1;
        g_atlas_cur_y = 1;
        g_atlas_row_h = 0;
    }
    if (g_atlas_slot_count >= MAX_VK_ATLAS_SLOTS) {
        g_atlas_slot_count = 0;
    }

    int32_t gx = g_atlas_cur_x;
    int32_t gy = g_atlas_cur_y;

    if (g_vk_staging_ptr) {
        if (g->is_color) {
            for (int32_t r = 0; r < g->height; r++) {
                const uint8_t *src_row = g->bitmap + (size_t)r * (size_t)g->stride;
                uint8_t *dst_row = g_vk_staging_ptr + (size_t)r * (size_t)g->width;
                for (int32_t c = 0; c < g->width; c++) {
                    uint8_t b = src_row[c * 4 + 0];
                    uint8_t gr = src_row[c * 4 + 1];
                    uint8_t re = src_row[c * 4 + 2];
                    uint8_t a = src_row[c * 4 + 3];
                    uint8_t lum = (uint8_t)((b * 29 + gr * 150 + re * 77 + 128) >> 8);
                    dst_row[c] = (a > 0) ? (lum > 30 ? lum : a) : 0;
                }
            }
        } else {
            for (int32_t r = 0; r < g->height; r++) {
                memcpy(g_vk_staging_ptr + (size_t)r * (size_t)g->width, g->bitmap + (size_t)r * (size_t)g->stride, (size_t)g->width);
            }
        }

        /* One-time copy command to atlas image */
        VkCommandBuffer cb;
        VkCommandBufferAllocateInfo cbai = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = g_vk_cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        vkAllocateCommandBuffers(g_vk_device, &cbai, &cb);

        VkCommandBufferBeginInfo cbbi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        vkBeginCommandBuffer(cb, &cbbi);

        static bool g_atlas_layout_initialized = false;
        VkImageMemoryBarrier imb = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = g_atlas_layout_initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = g_atlas_layout_initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = g_vk_atlas_img,
            .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        };
        vkCmdPipelineBarrier(cb,
                             g_atlas_layout_initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &imb);
        g_atlas_layout_initialized = true;

        VkBufferImageCopy bic = {
            .bufferOffset = 0,
            .bufferRowLength = (uint32_t)g->width,
            .bufferImageHeight = (uint32_t)g->height,
            .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
            .imageOffset = { gx, gy, 0 },
            .imageExtent = { (uint32_t)g->width, (uint32_t)g->height, 1 }
        };
        vkCmdCopyBufferToImage(cb, g_vk_staging_buf, g_vk_atlas_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);

        imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &imb);

        vkEndCommandBuffer(cb);

        VkSubmitInfo si = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb
        };
        vkQueueSubmit(g_vk_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(g_vk_queue);
        vkFreeCommandBuffers(g_vk_device, g_vk_cmd_pool, 1, &cb);
    }

    vk_atlas_slot_t *slot = &g_atlas_slots[g_atlas_slot_count++];
    slot->used = 1;
    slot->codepoint = cp;
    slot->style = style;
    slot->u0 = (float)gx / (float)VK_ATLAS_SIZE;
    slot->v0 = (float)gy / (float)VK_ATLAS_SIZE;
    slot->u1 = (float)(gx + g->width)  / (float)VK_ATLAS_SIZE;
    slot->v1 = (float)(gy + g->height) / (float)VK_ATLAS_SIZE;
    slot->bearing_x = g->bearing_x;
    slot->bearing_y = g->bearing_y;
    slot->width = g->width;
    slot->height = g->height;

    g_atlas_cur_x += g->width + 1;
    if (g->height > g_atlas_row_h) g_atlas_row_h = g->height;
    return slot;
}

/* Procedural Box-Drawing & Block Elements (100% Geometry Parity with Pixman & OpenGL) */
static bool vk_render_procedural_glyph(int32_t px, int32_t py, int32_t w, int32_t h,
                                       uint32_t cp, uint32_t fg_rgb, uint32_t bg_rgb) {
    (void)bg_rgb;
    if (cp < 0x2500 || cp > 0x259F) return false;

    int32_t t = h >= 28 ? 2 : 1;
    int32_t th = t * 2;
    int32_t mx = px + w / 2;
    int32_t my = py + h / 2;

    if (cp >= 0x2580 && cp <= 0x259F) {
        switch (cp) {
            case 0x2580: vk_fill_rect(px, py, w, h / 2, fg_rgb); return true;
            case 0x2581: vk_fill_rect(px, py + h * 7 / 8, w, h - h * 7 / 8, fg_rgb); return true;
            case 0x2582: vk_fill_rect(px, py + h * 3 / 4, w, h - h * 3 / 4, fg_rgb); return true;
            case 0x2583: vk_fill_rect(px, py + h * 5 / 8, w, h - h * 5 / 8, fg_rgb); return true;
            case 0x2584: vk_fill_rect(px, py + h / 2, w, h - h / 2, fg_rgb); return true;
            case 0x2585: vk_fill_rect(px, py + h * 3 / 8, w, h - h * 3 / 8, fg_rgb); return true;
            case 0x2586: vk_fill_rect(px, py + h / 4, w, h - h / 4, fg_rgb); return true;
            case 0x2587: vk_fill_rect(px, py + h / 8, w, h - h / 8, fg_rgb); return true;
            case 0x2588: vk_fill_rect(px, py, w, h, fg_rgb); return true;
            case 0x2589: vk_fill_rect(px, py, w * 7 / 8, h, fg_rgb); return true;
            case 0x258A: vk_fill_rect(px, py, w * 3 / 4, h, fg_rgb); return true;
            case 0x258B: vk_fill_rect(px, py, w * 5 / 8, h, fg_rgb); return true;
            case 0x258C: vk_fill_rect(px, py, w / 2, h, fg_rgb); return true;
            case 0x258D: vk_fill_rect(px, py, w * 3 / 8, h, fg_rgb); return true;
            case 0x258E: vk_fill_rect(px, py, w / 4, h, fg_rgb); return true;
            case 0x258F: vk_fill_rect(px, py, w / 8, h, fg_rgb); return true;
            case 0x2590: vk_fill_rect(px + w / 2, py, w - w / 2, h, fg_rgb); return true;
            case 0x2594: vk_fill_rect(px, py, w, h / 8, fg_rgb); return true;
            case 0x2595: vk_fill_rect(px + w * 7 / 8, py, w - w * 7 / 8, h, fg_rgb); return true;
            case 0x2591:
            case 0x2592:
            case 0x2593: {
                uint32_t step = (cp == 0x2591) ? 4 : ((cp == 0x2592) ? 2 : 1);
                for (int32_t dy = 0; dy < h; dy++) {
                    for (int32_t dx = 0; dx < w; dx++) {
                        if (((dx + dy) % step) == 0) {
                            vk_fill_rect(px + dx, py + dy, 1, 1, fg_rgb);
                        }
                    }
                }
                return true;
            }
            default: return false;
        }
    }

    int32_t l = 0, r = 0, u = 0, d = 0;
    switch (cp) {
        case 0x2500: l = 1; r = 1; break;
        case 0x2501: l = 2; r = 2; break;
        case 0x2502: u = 1; d = 1; break;
        case 0x2503: u = 2; d = 2; break;
        case 0x250C: r = 1; d = 1; break;
        case 0x250D: r = 2; d = 1; break;
        case 0x250E: r = 1; d = 2; break;
        case 0x250F: r = 2; d = 2; break;
        case 0x2510: l = 1; d = 1; break;
        case 0x2511: l = 2; d = 1; break;
        case 0x2512: l = 1; d = 2; break;
        case 0x2513: l = 2; d = 2; break;
        case 0x2514: u = 1; r = 1; break;
        case 0x2515: u = 1; r = 2; break;
        case 0x2516: u = 2; r = 1; break;
        case 0x2517: u = 2; r = 2; break;
        case 0x2518: u = 1; l = 1; break;
        case 0x2519: u = 1; l = 2; break;
        case 0x251A: u = 2; l = 1; break;
        case 0x251B: u = 2; l = 2; break;
        case 0x251C: u = 1; d = 1; r = 1; break;
        case 0x251D: u = 1; d = 1; r = 2; break;
        case 0x2520: u = 2; d = 2; r = 1; break;
        case 0x2523: u = 2; d = 2; r = 2; break;
        case 0x2524: u = 1; d = 1; l = 1; break;
        case 0x2525: u = 1; d = 1; l = 2; break;
        case 0x2528: u = 2; d = 2; l = 1; break;
        case 0x252B: u = 2; d = 2; l = 2; break;
        case 0x252C: l = 1; r = 1; d = 1; break;
        case 0x252F: l = 2; r = 2; d = 1; break;
        case 0x2530: l = 1; r = 1; d = 2; break;
        case 0x2533: l = 2; r = 2; d = 2; break;
        case 0x2534: l = 1; r = 1; u = 1; break;
        case 0x2537: l = 2; r = 2; u = 1; break;
        case 0x2538: l = 1; r = 1; u = 2; break;
        case 0x253B: l = 2; r = 2; u = 2; break;
        case 0x253C: l = 1; r = 1; u = 1; d = 1; break;
        case 0x253F: l = 2; r = 2; u = 1; d = 1; break;
        case 0x2542: l = 1; r = 1; u = 2; d = 2; break;
        case 0x254B: l = 2; r = 2; u = 2; d = 2; break;
        case 0x2550: l = 3; r = 3; break;
        case 0x2551: u = 3; d = 3; break;
        case 0x2554: r = 3; d = 3; break;
        case 0x2557: l = 3; d = 3; break;
        case 0x255A: r = 3; u = 3; break;
        case 0x255D: l = 3; u = 3; break;
        case 0x2560: u = 3; d = 3; r = 3; break;
        case 0x2563: u = 3; d = 3; l = 3; break;
        case 0x2566: l = 3; r = 3; d = 3; break;
        case 0x2569: l = 3; r = 3; u = 3; break;
        case 0x256C: l = 3; r = 3; u = 3; d = 3; break;
        case 0x2574: l = 1; break;
        case 0x2575: u = 1; break;
        case 0x2576: r = 1; break;
        case 0x2577: d = 1; break;
        case 0x2578: l = 2; break;
        case 0x2579: u = 2; break;
        case 0x257A: r = 2; break;
        case 0x257B: d = 2; break;
        default: return false;
    }

    if (l) {
        int32_t lw = l == 3 ? 1 : (l == 2 ? th : t);
        if (l == 3) {
            vk_fill_rect(px, my - t - 1, mx - px + 1, 1, fg_rgb);
            vk_fill_rect(px, my + t, mx - px + 1, 1, fg_rgb);
        } else {
            vk_fill_rect(px, my - lw / 2, mx - px + (r ? 1 : 0), lw, fg_rgb);
        }
    }
    if (r) {
        int32_t rw = r == 3 ? 1 : (r == 2 ? th : t);
        if (r == 3) {
            vk_fill_rect(mx, my - t - 1, px + w - mx, 1, fg_rgb);
            vk_fill_rect(mx, my + t, px + w - mx, 1, fg_rgb);
        } else {
            vk_fill_rect(mx - (l ? 0 : rw / 2), my - rw / 2, px + w - mx + (l ? 0 : rw / 2), rw, fg_rgb);
        }
    }
    if (u) {
        int32_t uw = u == 3 ? 1 : (u == 2 ? th : t);
        if (u == 3) {
            vk_fill_rect(mx - t - 1, py, 1, my - py + 1, fg_rgb);
            vk_fill_rect(mx + t, py, 1, my - py + 1, fg_rgb);
        } else {
            vk_fill_rect(mx - uw / 2, py, uw, my - py + (d ? 1 : 0), fg_rgb);
        }
    }
    if (d) {
        int32_t dw = d == 3 ? 1 : (d == 2 ? th : t);
        if (d == 3) {
            vk_fill_rect(mx - t - 1, my, 1, py + h - my, fg_rgb);
            vk_fill_rect(mx + t, my, 1, py + h - my, fg_rgb);
        } else {
            vk_fill_rect(mx - dw / 2, my - (u ? 0 : dw / 2), dw, py + h - my + (u ? 0 : dw / 2), fg_rgb);
        }
    }
    return true;
}

static void vk_cell_ext(int32_t px, int32_t py, int32_t cell_w, int32_t cell_h,
                        uint32_t codepoint, uint32_t combining, uint32_t fg_rgb, uint32_t bg_rgb,
                        uint32_t attr_flags, int32_t span_cols,
                        uint8_t ul_style, uint32_t ul_color) {
    (void)combining;
    if (span_cols < 1) span_cols = 1;
    int32_t total_w = cell_w * span_cols;

    /* 1. Background */
    if (bg_rgb != 0xFFFFFFFFU) {
        vk_fill_rect(px, py, total_w, cell_h, bg_rgb);
    }

    /* 2. Procedural Box-Drawing & Block Elements */
    if (vk_render_procedural_glyph(px, py, total_w, cell_h, codepoint, fg_rgb, bg_rgb)) {
        /* rendered procedurally with pixel precision */
    } else if (codepoint > 0x20U) {
        uint8_t style = (uint8_t)(attr_flags & 0x03U);
        vk_atlas_slot_t *slot = vk_get_or_upload_glyph(codepoint, style);
        if (slot) {
            float gx = (float)(px + slot->bearing_x);
            float gy = (float)(py + goosy_font_baseline() - slot->bearing_y);
            uint32_t argb = 0xFF000000U | (fg_rgb & 0x00FFFFFFU);
            vk_push_quad(gx, gy, (float)slot->width, (float)slot->height,
                         slot->u0, slot->v0, slot->u1, slot->v1, argb);
        }
    }

    /* 3. Underline / Strike */
    uint32_t line_color = (ul_color != 0) ? ul_color : fg_rgb;
    int32_t base_y = py + goosy_font_baseline();
    int32_t line_th = cell_h >= 28 ? 2 : 1;

    if (ul_style == 1 || (attr_flags & 0x04U)) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        vk_fill_rect(px, uy, total_w, line_th, line_color);
    } else if (ul_style == 2) {
        int32_t uy1 = base_y + 1;
        int32_t uy2 = base_y + 3;
        if (uy2 + line_th > py + cell_h) { uy2 = py + cell_h - line_th; uy1 = uy2 - 2; }
        vk_fill_rect(px, uy1, total_w, line_th, line_color);
        vk_fill_rect(px, uy2, total_w, line_th, line_color);
    } else if (ul_style == 3) {
        int32_t uy = base_y + 2;
        if (uy + 3 > py + cell_h) uy = py + cell_h - 3;
        for (int32_t cx = 0; cx < total_w; cx++) {
            int32_t wave = (cx % 4 < 2) ? 0 : 2;
            vk_fill_rect(px + cx, uy + wave, 1, 1, line_color);
        }
    } else if (ul_style == 4) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        for (int32_t cx = 0; cx < total_w; cx += 3) {
            vk_fill_rect(px + cx, uy, 1, line_th, line_color);
        }
    } else if (ul_style == 5) {
        int32_t uy = base_y + 2;
        if (uy + line_th > py + cell_h) uy = py + cell_h - line_th;
        for (int32_t cx = 0; cx < total_w; cx += 6) {
            int32_t dw = (cx + 3 <= total_w) ? 3 : (total_w - cx);
            vk_fill_rect(px + cx, uy, dw, line_th, line_color);
        }
    }

    if (attr_flags & 0x20U) {
        int32_t sy = py + cell_h / 2;
        vk_fill_rect(px, sy, total_w, line_th, fg_rgb);
    }
}

static void vk_cursor(int32_t cx_px, int32_t cy_px, int32_t cell_w, int32_t cell_h,
                      uint32_t color_rgb, int32_t style) {
    int32_t line_th = (cell_h >= 28) ? 2 : 1;
    switch (style) {
        case 0:
            vk_fill_rect(cx_px, cy_px, cell_w, cell_h, color_rgb);
            break;
        case 1: {
            int32_t uy = cy_px + cell_h - line_th * 2;
            vk_fill_rect(cx_px, uy, cell_w, line_th * 2, color_rgb);
            break;
        }
        case 2:
            vk_fill_rect(cx_px, cy_px, line_th * 2, cell_h, color_rgb);
            break;
        case 3:
            vk_fill_rect(cx_px, cy_px, cell_w, line_th, color_rgb);
            vk_fill_rect(cx_px, cy_px + cell_h - line_th, cell_w, line_th, color_rgb);
            vk_fill_rect(cx_px, cy_px, line_th, cell_h, color_rgb);
            vk_fill_rect(cx_px + cell_w - line_th, cy_px, line_th, cell_h, color_rgb);
            break;
        default:
            vk_fill_rect(cx_px, cy_px, cell_w, cell_h, color_rgb);
            break;
    }
}

static int32_t vk_title_icon(int32_t x, int32_t y, int32_t size_px) {
    int count = 0;
    const goosy_icon_span_t *spans = goosy_get_icon_spans(size_px, &count);
    if (!spans || count <= 0) return 0;
    for (int i = 0; i < count; i++) {
        vk_push_quad((float)(x + spans[i].x), (float)(y + spans[i].y),
                     (float)spans[i].w, 1.0f,
                     -1.0f, -1.0f, -1.0f, -1.0f, spans[i].argb);
    }
    return 1;
}

static void vk_images(int32_t pad_px, int32_t title_h, int32_t view_top_abs_row,
                      int32_t rows, int32_t cols, int32_t cell_w, int32_t cell_h) {
    (void)pad_px; (void)title_h; (void)view_top_abs_row;
    (void)rows; (void)cols; (void)cell_w; (void)cell_h;
}

static void vk_damage(int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)x; (void)y; (void)w; (void)h;
}

static void vk_resize(int32_t width_px, int32_t height_px, int32_t scale);

static int32_t vk_begin(uint32_t bg_rgb) {
    if (!g_vk_device || !g_vk_swapchain) return -1;

    vkWaitForFences(g_vk_device, 1, &g_vk_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(g_vk_device, 1, &g_vk_fence);

    VkResult res = vkAcquireNextImageKHR(g_vk_device, g_vk_swapchain, UINT64_MAX, g_vk_sem_available, VK_NULL_HANDLE, &g_active_image_idx);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        vk_resize(g_cur_w, g_cur_h, g_cur_scale);
        res = vkAcquireNextImageKHR(g_vk_device, g_vk_swapchain, UINT64_MAX, g_vk_sem_available, VK_NULL_HANDLE, &g_active_image_idx);
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        return -1;
    }
    if (g_active_image_idx >= g_vk_image_count) {
        return -1;
    }

    g_quad_count = 0;
    g_vk_bg_rgb = bg_rgb;
    return 0;
}

static void vk_end(bool request_frame_cb) {
    (void)request_frame_cb;
    if (!g_vk_device || !g_vk_swapchain) return;
    if (g_active_image_idx >= g_vk_image_count) return;

    VkCommandBuffer cb = g_vk_cmd_bufs[g_active_image_idx];
    if (!cb) return;
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(cb, &cbbi);

    float clr_r = ((g_vk_bg_rgb >> 16) & 0xFF) / 255.0f;
    float clr_g = ((g_vk_bg_rgb >> 8)  & 0xFF) / 255.0f;
    float clr_b = (g_vk_bg_rgb & 0xFF)         / 255.0f;
    VkClearValue cv = { .color = { .float32 = { clr_r, clr_g, clr_b, 1.0f } } };
    VkRenderPassBeginInfo rpbi = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_vk_render_pass,
        .framebuffer = g_vk_framebuffers[g_active_image_idx],
        .renderArea = { {0, 0}, g_vk_extent },
        .clearValueCount = 1,
        .pClearValues = &cv
    };
    vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = { 0.0f, 0.0f, (float)g_vk_extent.width, (float)g_vk_extent.height, 0.0f, 1.0f };
    VkRect2D sc = { {0, 0}, g_vk_extent };
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);

    if (g_quad_count > 0) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk_pipeline);

        vk_push_const_t pc = { .u_viewport = { (float)g_vk_extent.width, (float)g_vk_extent.height } };
        vkCmdPushConstants(cb, g_vk_pipe_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_vk_pipe_layout, 0, 1, &g_vk_desc_set, 0, NULL);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &g_vk_vbo, &offset);
        vkCmdBindIndexBuffer(cb, g_vk_ebo, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cb, (uint32_t)(g_quad_count * 6), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vk_sem_available,
        .pWaitDstStageMask = &wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &cb,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &g_vk_sem_rendered
    };
    vkQueueSubmit(g_vk_queue, 1, &si, g_vk_fence);

    VkPresentInfoKHR pi = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &g_vk_sem_rendered,
        .swapchainCount = 1,
        .pSwapchains = &g_vk_swapchain,
        .pImageIndices = &g_active_image_idx
    };
    VkResult pres_res = vkQueuePresentKHR(g_vk_queue, &pi);
    if (pres_res == VK_ERROR_OUT_OF_DATE_KHR || pres_res == VK_SUBOPTIMAL_KHR) {
        vk_resize(g_cur_w, g_cur_h, g_cur_scale);
    }
    if (request_frame_cb) {
        goosy_wl_request_frame_callback();
    }
    g_quad_count = 0;
}

static void vk_resize(int32_t width_px, int32_t height_px, int32_t scale) {
    g_cur_w = width_px;
    g_cur_h = height_px;
    g_cur_scale = scale > 0 ? scale : 1;
    if (g_vk_device && g_vk_swapchain) {
        vkDeviceWaitIdle(g_vk_device);
        for (uint32_t i = 0; i < g_vk_image_count; i++) {
            if (g_vk_framebuffers[i]) { vkDestroyFramebuffer(g_vk_device, g_vk_framebuffers[i], NULL); g_vk_framebuffers[i] = VK_NULL_HANDLE; }
            if (g_vk_views[i])        { vkDestroyImageView(g_vk_device, g_vk_views[i], NULL); g_vk_views[i] = VK_NULL_HANDLE; }
        }
        if (g_vk_swapchain) { vkDestroySwapchainKHR(g_vk_device, g_vk_swapchain, NULL); g_vk_swapchain = VK_NULL_HANDLE; }
        create_swapchain_and_views();
    }
}

static void vk_cleanup(void) {
    if (g_vk_device) {
        vkDeviceWaitIdle(g_vk_device);
        if (g_vk_fence)          { vkDestroyFence(g_vk_device, g_vk_fence, NULL); g_vk_fence = VK_NULL_HANDLE; }
        if (g_vk_sem_available)  { vkDestroySemaphore(g_vk_device, g_vk_sem_available, NULL); g_vk_sem_available = VK_NULL_HANDLE; }
        if (g_vk_sem_rendered)   { vkDestroySemaphore(g_vk_device, g_vk_sem_rendered, NULL); g_vk_sem_rendered = VK_NULL_HANDLE; }
        if (g_vk_cmd_pool)       { vkDestroyCommandPool(g_vk_device, g_vk_cmd_pool, NULL); g_vk_cmd_pool = VK_NULL_HANDLE; }
        if (g_vk_staging_buf)    { vkDestroyBuffer(g_vk_device, g_vk_staging_buf, NULL); g_vk_staging_buf = VK_NULL_HANDLE; }
        if (g_vk_staging_mem)    { vkFreeMemory(g_vk_device, g_vk_staging_mem, NULL); g_vk_staging_mem = VK_NULL_HANDLE; }
        if (g_vk_atlas_sampler)  { vkDestroySampler(g_vk_device, g_vk_atlas_sampler, NULL); g_vk_atlas_sampler = VK_NULL_HANDLE; }
        if (g_vk_atlas_view)     { vkDestroyImageView(g_vk_device, g_vk_atlas_view, NULL); g_vk_atlas_view = VK_NULL_HANDLE; }
        if (g_vk_atlas_img)      { vkDestroyImage(g_vk_device, g_vk_atlas_img, NULL); g_vk_atlas_img = VK_NULL_HANDLE; }
        if (g_vk_atlas_mem)      { vkFreeMemory(g_vk_device, g_vk_atlas_mem, NULL); g_vk_atlas_mem = VK_NULL_HANDLE; }
        if (g_vk_desc_pool)      { vkDestroyDescriptorPool(g_vk_device, g_vk_desc_pool, NULL); g_vk_desc_pool = VK_NULL_HANDLE; }
        if (g_vk_desc_layout)    { vkDestroyDescriptorSetLayout(g_vk_device, g_vk_desc_layout, NULL); g_vk_desc_layout = VK_NULL_HANDLE; }
        if (g_vk_ebo)            { vkDestroyBuffer(g_vk_device, g_vk_ebo, NULL); g_vk_ebo = VK_NULL_HANDLE; }
        if (g_vk_ebo_mem)        { vkFreeMemory(g_vk_device, g_vk_ebo_mem, NULL); g_vk_ebo_mem = VK_NULL_HANDLE; }
        if (g_vk_vbo)            { vkDestroyBuffer(g_vk_device, g_vk_vbo, NULL); g_vk_vbo = VK_NULL_HANDLE; }
        if (g_vk_vbo_mem)        { vkFreeMemory(g_vk_device, g_vk_vbo_mem, NULL); g_vk_vbo_mem = VK_NULL_HANDLE; }
        for (uint32_t i = 0; i < g_vk_image_count; i++) {
            if (g_vk_framebuffers[i]) vkDestroyFramebuffer(g_vk_device, g_vk_framebuffers[i], NULL);
            if (g_vk_views[i])        vkDestroyImageView(g_vk_device, g_vk_views[i], NULL);
        }
        if (g_vk_pipeline)       { vkDestroyPipeline(g_vk_device, g_vk_pipeline, NULL); g_vk_pipeline = VK_NULL_HANDLE; }
        if (g_vk_pipe_layout)    { vkDestroyPipelineLayout(g_vk_device, g_vk_pipe_layout, NULL); g_vk_pipe_layout = VK_NULL_HANDLE; }
        if (g_vk_render_pass)    { vkDestroyRenderPass(g_vk_device, g_vk_render_pass, NULL); g_vk_render_pass = VK_NULL_HANDLE; }
        if (g_vk_swapchain)      { vkDestroySwapchainKHR(g_vk_device, g_vk_swapchain, NULL); g_vk_swapchain = VK_NULL_HANDLE; }
        vkDestroyDevice(g_vk_device, NULL);
        g_vk_device = VK_NULL_HANDLE;
    }
    if (g_vk_surface)  { vkDestroySurfaceKHR(g_vk_instance, g_vk_surface, NULL); g_vk_surface = VK_NULL_HANDLE; }
    if (g_vk_instance) { vkDestroyInstance(g_vk_instance, NULL); g_vk_instance = VK_NULL_HANDLE; }
}

goosy_renderer_backend_t g_backend_vk = {
    .name       = "Vulkan (Hardware)",
    .type       = GOOSY_RENDERER_VULKAN,
    .init       = vk_init,
    .begin      = vk_begin,
    .fill_rect  = vk_fill_rect,
    .cell_ext   = vk_cell_ext,
    .cursor     = vk_cursor,
    .title_icon = vk_title_icon,
    .images     = vk_images,
    .damage     = vk_damage,
    .end        = vk_end,
    .cleanup    = vk_cleanup,
    .resize     = vk_resize
};
