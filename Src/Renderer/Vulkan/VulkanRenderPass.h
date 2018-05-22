#pragma once

#include <vector>
#include <map>
#include <vulkan/vulkan.hpp>
#include "VulkanCommandBuffer.h"
#include "VulkanRenderItem.hpp"
#include "VulkanGraphicsPipeline.h"
#include "VulkanImage.h"

struct VulkanRenderTarget
{
	vk::AttachmentLoadOp LoadOp = vk::AttachmentLoadOp::eClear;
	vk::AttachmentStoreOp StoreOp = vk::AttachmentStoreOp::eStore;

	vk::ImageLayout InitialLayout = vk::ImageLayout::eUndefined;
	vk::ImageLayout UsageLayout = vk::ImageLayout::eColorAttachmentOptimal;
	vk::ImageLayout FinalLayout = vk::ImageLayout::eColorAttachmentOptimal;

	//1 Image per back buffer
	std::vector<vk::ImageView*> ImageViews;
	vk::Format Format;
};

class VulkanRenderPass
{
public:

	VulkanRenderPass();
	
	vk::RenderPass GetHandle() {return RenderPass.get();}

	std::vector<vk::UniqueFramebuffer>& GetFramebuffers() { return Framebuffers; }

	//Used by frame graph to build this render target
	void BuildRenderPass(std::vector<VulkanRenderTarget*> RenderTargets, VulkanRenderTarget* DepthTarget, uint32_t Width, uint32_t Height, uint32_t BackbufferCount);

	//Builds a secondary command buffer for this render pass
	void BuildCommandBuffer(std::vector<std::pair<VulkanRenderItem*, VulkanGraphicsPipeline*>> ItemsToRender);
	VulkanCommandBuffer& GetCommandBuffer() { return CommandBuffer; }

	//Adds commands to command buffer
	void RecordCommands(VulkanCommandBuffer& CommandBuffer, size_t FrameIndex);

protected:

	vk::UniqueRenderPass RenderPass;

	std::vector<vk::UniqueFramebuffer> Framebuffers;

	/** Secondary command buffer that orchestrates pipeline binds and render calls */
	VulkanCommandBuffer CommandBuffer;

	vk::Extent2D Extent;
};