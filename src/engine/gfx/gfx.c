#include "common.h"
#include "engine.h"
#include "array.h"
#include "util/file.h"
#include "res/res.h"
#include "win/win.h"
#include "event/event.h"

#include "gfx/gfx.h"
#include "gfx/gfx_types.h"
#include "gfx/vk_util.h"
#include "gfx/vk_instance.h"
#include "gfx/vk_device.h"
#include "gfx/vk_swapchain.h"

#include <errno.h>
#include <unistd.h>

#include <vk_mem_alloc.h>
#include <stb_image.h>

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

/***********
 * GLOBALS *
 ***********/

struct VkEngine vk;
void vk_create_depth_resources(void);


static void vk_create_renderpass(void)
{
	VkAttachmentDescription color_attachment = {
		.format  = vk.swapchain_img_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	VkAttachmentReference color_attachment_reference = {
		.attachment = 0,
		.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	};

	VkAttachmentDescription depth_attachment = {
		.format  = vk_find_depth_format(),
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};
	VkAttachmentReference depth_attachment_reference = {
		.attachment = 1,
		.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	};

	VkSubpassDescription subpass = {
		.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments    = &color_attachment_reference,

		.pDepthStencilAttachment = &depth_attachment_reference,
	};

	VkSubpassDependency dep[] = {
		{
			.srcSubpass     = VK_SUBPASS_EXTERNAL,
			.dstSubpass     = 0,
			.srcStageMask   = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
							| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask  = 0,
			.dstStageMask   = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
							| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.dstAccessMask  = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
							| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		}
	};

	/*
	VkSubpassDependency dep[] = {
		{
			.srcSubpass     = VK_SUBPASS_EXTERNAL,
			.dstSubpass     = 0,

			.srcStageMask   = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			.dstStageMask   = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,

			.srcAccessMask  = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask  = VK_ACCESS_SHADER_READ_BIT,

			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		},
		{
			.srcSubpass     = 0,
			.dstSubpass     = VK_SUBPASS_EXTERNAL,

			.srcStageMask   = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstStageMask   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,

			.srcAccessMask  = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask  = VK_ACCESS_MEMORY_READ_BIT,

			.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
		}
	};
	*/

	VkAttachmentDescription attachments[] = {color_attachment, depth_attachment};
	VkRenderPassCreateInfo render_pass_info = {
		.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.pAttachments    = attachments,
		.attachmentCount = LENGTH(attachments),
		.subpassCount    = 1,
		.pSubpasses      = &subpass,
		.dependencyCount = LENGTH(dep),
		.pDependencies   = dep
	};
	
	VkResult ret = vkCreateRenderPass(vk.dev, &render_pass_info, NULL, &vk.renderpass);

	if(ret != VK_SUCCESS) engine_crash("vkCreateRenderPass failed");
}

/***************
 * FRAMEBUFFER *
 ***************/ 

void vk_create_framebuffers(void)
{
	vk.framebuffers_num = vk.swapchain_img_num;
	vk.framebuffers     = malloc(sizeof(VkFramebuffer) * vk.framebuffers_num);

	for(size_t i = 0; i < vk.framebuffers_num; i++){
		VkImageView attachments[] = {
			vk.swapchain_img_view[i],
			vk.depth_view,
		};
		
		VkFramebufferCreateInfo fb_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = vk.renderpass,
			.attachmentCount = LENGTH(attachments),
			.pAttachments = attachments,
			.width = vk.swapchain_extent.width,
			.height = vk.swapchain_extent.height,
			.layers = 1,
		};
		
		VkResult ret = vkCreateFramebuffer(vk.dev, &fb_info, NULL, &vk.framebuffers[i]);
		if(ret != VK_SUCCESS) engine_crash("vkCreateFramebuffer failed");
	}
}

/************
 * COMMANDS *
 ************/

void vk_create_cmd_pool(void)
{
	VkResult ret; 

	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = vk.family_graphics,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
		       | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT // Is this a good idea?
	};
	ret = vkCreateCommandPool(vk.dev, &pool_info, NULL, &vk.cmd_pool);
	if(ret != VK_SUCCESS) engine_crash("vkCreateCommandPool failed");
}

/**********
 * FRAMES *
 *********/

void vk_init_frame( struct VkFrame *frame )
{
	VkResult ret; 

	/*
	 * Command pool and buffer
	 */

	VkCommandPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = vk.family_graphics,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
		       | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT // Is this a good idea?
	};
	ret = vkCreateCommandPool(vk.dev, &pool_info, NULL, &frame->cmd_pool);
	if(ret != VK_SUCCESS) engine_crash("vkCreateCommandPool failed");
	
	VkCommandBufferAllocateInfo alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool        = frame->cmd_pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	ret = vkAllocateCommandBuffers(vk.dev, &alloc_info, &frame->cmd_buf);
	if(ret != VK_SUCCESS) engine_crash("vkAllocateCommandBuffers failed");
	
	/*
	 * Sync
	 */

	VkSemaphoreCreateInfo sema_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	ret = vkCreateSemaphore(vk.dev, &sema_info, NULL, &frame->image_available);
	if(ret != VK_SUCCESS) engine_crash("vkCreateSemaphore failed");

	ret = vkCreateSemaphore(vk.dev, &sema_info, NULL, &frame->render_finished);
	if(ret != VK_SUCCESS) engine_crash("vkCreateSemaphore failed");

	VkFenceCreateInfo fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	ret = vkCreateFence(vk.dev, &fence_info, NULL, &frame->flight);
	if(ret != VK_SUCCESS) engine_crash("vkCreateFence failed");

}


void vk_destroy_frame( struct VkFrame *frame )
{
	vkDestroySemaphore  (vk.dev, frame->image_available, NULL);
	vkDestroySemaphore  (vk.dev, frame->render_finished, NULL);
	vkDestroyFence      (vk.dev, frame->flight,          NULL);
	vkDestroyCommandPool(vk.dev, frame->cmd_pool, NULL);
}

void vk_create_sync(void)
{
	vk.fence_image = calloc( sizeof(vk.swapchain_img_num), sizeof(VkFence) );
	return;
}


void vk_create_descriptor_pool(void)
{
	VkDescriptorPoolSize pool_sizes[] = {
		{
			.type  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 32,
		},

		{
			.type  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 32,
		},
		{
			.type  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 32,
		},
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = LENGTH(pool_sizes),
		.pPoolSizes = pool_sizes,
		.maxSets = 64,
		.flags  = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
	};

	VkResult ret;
	ret = vkCreateDescriptorPool(vk.dev, &pool_info, NULL, &vk.descriptor_pool);
	if (ret != VK_SUCCESS) engine_crash("vkCreateDescriptorPool failed");
}


void vk_recreate_swapchain(void)
{
	int width = 0, height = 0;
	glfwGetFramebufferSize(vk.window, &width, &height);
	while(width==0 || height == 0){
		glfwGetFramebufferSize(vk.window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(vk.dev);

	event_fire(EVENT_VK_SWAPCHAIN_DESTROY, NULL);

	vk_destroy_swapchain();
	vk_create_swapchain();
	vk_create_image_views();

	vk_create_depth_resources();
	vk_create_framebuffers();

	event_fire(EVENT_VK_SWAPCHAIN_CREATE, NULL);
	vk.framebuffer_resize = false;
}


struct VkFrame *gfx_frame_get(void)
{
	VkResult ret;

	struct VkFrame *restrict frame = &vk.frames[vk.current_frame];

	vkWaitForFences(vk.dev, 1, &frame->flight, VK_TRUE, UINT64_MAX);
    ret = vkAcquireNextImageKHR(vk.dev, vk.swapchain, 
			UINT64_MAX, 
			frame->image_available,
			VK_NULL_HANDLE, 
			&frame->image_index);

	if (ret == VK_ERROR_OUT_OF_DATE_KHR ) {
		log_debug("Recreate");
		vk_recreate_swapchain();
	} else if (ret != VK_SUCCESS && ret != VK_SUBOPTIMAL_KHR) {
		engine_crash("Unhandled image capture error");
	}

	if (vk.fence_image[frame->image_index] != VK_NULL_HANDLE) {
		vkWaitForFences(vk.dev, 1, &vk.fence_image[frame->image_index], VK_TRUE, UINT64_MAX);
	}
	vk.fence_image[frame->image_index] = frame->flight;

	/* Command buffer stuff */

	VkCommandBuffer cmd = frame->cmd_buf;
	vkResetCommandBuffer(cmd, 0);

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = 0,
		.pInheritanceInfo = NULL,
	};
	ret = vkBeginCommandBuffer(cmd, &begin_info);
	if(ret != VK_SUCCESS) engine_crash("vkBeginCommandBuffer failed");



	return frame;
}

void gfx_frame_mainpass_begin(struct VkFrame *frame)
{
	VkCommandBuffer cmd = frame->cmd_buf;

	VkClearValue clear_color[] = {
		{{{0.0f, 0.0f, 0.0f, 1.0f}}},
		{{{1.0,  0.0f}}}
	};

	VkRenderPassBeginInfo pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass  = vk.renderpass,
		.framebuffer = vk.framebuffers[frame->image_index],
		.renderArea.offset = {0,0},
		.renderArea.extent = vk.swapchain_extent,
		.clearValueCount   = LENGTH(clear_color),
		.pClearValues = clear_color,
	};
	vkCmdBeginRenderPass(cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

	VkRect2D scissor = {
		.offset = {.x=0, .y=0},
		.extent = vk.swapchain_extent,
	};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	VkViewport viewport = {
		.x        = 0.0,
		.y        = 0.0,
		.width    = scissor.extent.width,
		.height   = scissor.extent.height,
		.minDepth = 0.0,
		.maxDepth = 1.0,
	};

	vkCmdSetViewport(cmd, 0, 1, &viewport);
}

void gfx_frame_mainpass_end(struct VkFrame *frame)
{
	vkCmdEndRenderPass(frame->cmd_buf);
}


void gfx_frame_submit(struct VkFrame *frame)
{
	VkResult ret;

	ret = vkEndCommandBuffer(frame->cmd_buf);
	
	if(ret != VK_SUCCESS) engine_crash("vkEndCommandBuffer failed");

	VkSemaphore          wait_semas[]  = {frame->image_available};
	VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore          sig_semas[]   = {frame->render_finished};

	VkSubmitInfo submit_info = {
		.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores    = wait_semas,
		.pWaitDstStageMask  = wait_stages,
		.commandBufferCount = 1,
		.pCommandBuffers    = &frame->cmd_buf,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores  = sig_semas, 
			
	};
	
	vkResetFences  (vk.dev, 1, &frame->flight);
	ret = vkQueueSubmit(vk.graphics_queue, 1, &submit_info, frame->flight);
	if (ret != VK_SUCCESS) engine_crash("vkQueueSubmit failed");

	VkSwapchainKHR swap_chains[] = {vk.swapchain};
	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores    = sig_semas,
		.swapchainCount     = 1,
		.pSwapchains        = swap_chains,
		.pImageIndices      = &frame->image_index,
		.pResults           = NULL,
	};
	ret = vkQueuePresentKHR(vk.present_queue, &present_info);

	if (ret == VK_ERROR_OUT_OF_DATE_KHR 
	 || ret == VK_SUBOPTIMAL_KHR 
	 || vk.framebuffer_resize)
	{
		vk_recreate_swapchain();
	} else if (ret != VK_SUCCESS) {
		engine_crash("vkQueuePresentKHR failed");
	}

	vk.current_frame = (vk.current_frame+1) % VK_FRAMES;
}

void vk_create_texture_sampler(void)
{
	VkSamplerCreateInfo sampler_info = {
		.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter               = VK_FILTER_LINEAR,
		.minFilter               = VK_FILTER_LINEAR,
		.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable        = VK_TRUE,
		.maxAnisotropy           = vk.dev_properties.limits.maxSamplerAnisotropy,
		.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.compareEnable           = VK_FALSE,
		.compareOp               = VK_COMPARE_OP_ALWAYS,
		.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.mipLodBias              = 0.0f,
		.minLod                  = 0.0f,
		.maxLod                  = 0.0f,
	};

	VkResult ret = vkCreateSampler(vk.dev, &sampler_info, NULL, &vk.texture_sampler);

	if(ret != VK_SUCCESS) engine_crash("vkCreateSampler failed");
}

void vk_create_depth_resources(void)
{
	VkFormat depth_format = vk_find_depth_format();

	vk_create_image_vma(
		vk.swapchain_extent.width,
		vk.swapchain_extent.height,
		depth_format,
		VK_IMAGE_TILING_OPTIMAL, 
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 
		VMA_MEMORY_USAGE_GPU_ONLY,
		&vk.depth_image,
		&vk.depth_alloc
	);

	VkImageViewCreateInfo create_info = {
		.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image    = vk.depth_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format   = depth_format,

		.components.r = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.g = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.b = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.a = VK_COMPONENT_SWIZZLE_IDENTITY,

		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
		.subresourceRange.baseMipLevel = 0,
		.subresourceRange.levelCount = 1,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount = 1,
	};

	VkResult ret = vkCreateImageView(vk.dev, &create_info, NULL, &vk.depth_view);
	if (ret != VK_SUCCESS) engine_crash("vkCreateImageView failed");
}


void vk_create_allocator(void)
{
	VmaAllocatorCreateInfo vma_info = {
		.vulkanApiVersion = VK_API_VERSION_1_2,
		.physicalDevice   = vk.dev_physical,
		.device           = vk.dev,
		.instance         = vk.instance,
	};
	VkResult ret = vmaCreateAllocator(&vma_info, &vk.vma);
	if(ret != VK_SUCCESS) engine_crash("vmaCreateAllocator failed");
}



static void vk_resize_callback(Handle handle, void*arg)
{
	vk.framebuffer_resize = true;
}

void gfx_init(void)
{
	memset( &vk, 0, sizeof(struct VkEngine) );
	vk._verbose = true;

	if (!glfwVulkanSupported()) 
		engine_crash("Vulkan is not supported");

	vk.window = win_get();
	event_bind(EVENT_WIN_RESIZE, vk_resize_callback, 0);

	vk_instance_ext_get_avbl();
	vk_validation_get_avbl();

#ifndef NDEBUG
	vk.debug = true;
#endif

	if (vk.debug) {
		vk_instance_ext_add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		vk_validation_add("VK_LAYER_KHRONOS_validation");
		vk_device_ext_add(VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);;
	}

	vk_create_instance(); 

	glfwCreateWindowSurface(vk.instance, vk.window, NULL, &vk.surface);

	vk_device_ext_add(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	vk_create_device();
	vkGetPhysicalDeviceProperties(vk.dev_physical, &vk.dev_properties);
	

	{
		/* Make name */
		static const char *types[] = {
			[VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU]    = "VIRTUAL",
			[VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU]   = "DISCRETE",
			[VK_PHYSICAL_DEVICE_TYPE_CPU]            = "CPU",
			[VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU] = "INTEGRATED",
			[VK_PHYSICAL_DEVICE_TYPE_OTHER]          = "OTHER"
		};

		snprintf(vk.dev_name, sizeof(vk.dev_name), "%s %s Vulkan %i.%i",
			types[vk.dev_properties.deviceType],
			vk.dev_properties.deviceName,
			(vk.dev_properties.apiVersion>>22) & 0x7FU,
			(vk.dev_properties.apiVersion>>12) & 0x3FFU

		);
	}

	vk_create_allocator();
	vk_create_swapchain();
	vk_create_image_views();
	vk_create_texture_sampler();

	vk_create_renderpass();

	vk_create_cmd_pool();
	vk_create_depth_resources();
	vk_create_framebuffers();

	vk_create_descriptor_pool();
	vk_create_sync();

	for(int i = 0; i < VK_FRAMES; i++){
		vk_init_frame( &vk.frames[i] );
		vk.frames[i].id = i;
	}

	log_debug("Swapchain images: %i", vk.swapchain_img_num);
}

void gfx_destroy()
{
	vk_destroy_swapchain();
	vkDestroyRenderPass(vk.dev, vk.renderpass, NULL);
	vkDestroySampler(vk.dev, vk.texture_sampler, NULL);
	for(int i = 0; i < VK_FRAMES; i++)
		vk_destroy_frame( &vk.frames[i] );

	vkDestroyDescriptorPool(vk.dev, vk.descriptor_pool, NULL);
	vkDestroyCommandPool(vk.dev, vk.cmd_pool, NULL);
	vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
	vmaDestroyAllocator(vk.vma);
	vkDestroyDevice(vk.dev, NULL);
	vk_destroy_instance();
}

