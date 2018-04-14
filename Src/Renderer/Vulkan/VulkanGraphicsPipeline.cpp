#include "VulkanGraphicsPipeline.h"
#include <string>
#include <iostream>
#include <fstream>
#include <map>
#include <algorithm>

#include "VulkanContext.h"
#include "VulkanRenderPass.h"
#include "spirv_reflect.h"

VulkanGraphicsPipeline::VulkanGraphicsPipeline()
{

}

VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
{
}

void VulkanGraphicsPipeline::BuildPipeline(VulkanRenderPass& RenderPass, const std::string& VertexShader, const std::string& FragmentShader)
{	
	//TODO: Add additional shader stages

	vk::GraphicsPipelineCreateInfo CreateInfo;

	std::vector<char> VertexSpirV = LoadShaderFromFile(VertexShader);
	vk::ShaderModule VertModule = CreateShaderModule(VertexSpirV);
	vk::PipelineShaderStageCreateInfo VertStageCreateInfo;
	VertStageCreateInfo.stage = vk::ShaderStageFlagBits::eVertex;
	VertStageCreateInfo.module = VertModule;
	VertStageCreateInfo.pName = "main";

	//Use SpirV_Reflect to build up our vertex input information
	SpvReflectShaderModule VertexShaderReflection;
	SPV_REFLECT_ASSERT(spvReflectCreateShaderModule(VertexSpirV.size(), VertexSpirV.data(), &VertexShaderReflection));

	uint32_t VertexInputCount = 0;
	SPV_REFLECT_ASSERT(spvReflectEnumerateInputVariables(&VertexShaderReflection, &VertexInputCount, nullptr));

	if (VertexInputCount > 0)
	{
		std::vector<SpvReflectInterfaceVariable*> VertexInputs(VertexInputCount);
		SPV_REFLECT_ASSERT(spvReflectEnumerateInputVariables(&VertexShaderReflection, &VertexInputCount, VertexInputs.data()));

		//TODO: will need to sort by binding THEN location to handle multiple vertex buffers
		std::sort(std::begin(VertexInputs), std::end(VertexInputs),
		[](const SpvReflectInterfaceVariable* a, const SpvReflectInterfaceVariable* b) 
		{
			return a->location < b->location; 
		});

		uint32_t CurrentOffset = 0;

		//Individual elements of our vertices
		std::vector<vk::VertexInputAttributeDescription> InputAttributes;
		for (auto& Input : VertexInputs)
		{
			vk::VertexInputAttributeDescription Attribute;
			Attribute.binding = 0; //TODO: Allow multiple vertex buffer bindings
			Attribute.location = Input->location;
			Attribute.format   = (vk::Format)Input->format;
			Attribute.offset = CurrentOffset;

			InputAttributes.push_back(Attribute);
			CurrentOffset += spv_reflect::FormatSize((VkFormat) Attribute.format);		
		}

		//Represents one type of Vertex for an input vertex buffer
		vk::VertexInputBindingDescription VertexBinding;
		VertexBinding.binding = 0; //TODO: Allow multiple vertex buffer bindings
		VertexBinding.stride = CurrentOffset;
		VertexBinding.inputRate = vk::VertexInputRate::eVertex;

		std::vector<vk::VertexInputBindingDescription> VertexBindingArray;
		VertexBindingArray.push_back(VertexBinding);

		SetVertexInputBindings(VertexBindingArray, InputAttributes);
	}

	std::vector<char> FragmentSpirV = LoadShaderFromFile(FragmentShader);
	vk::ShaderModule FragModule = CreateShaderModule(FragmentSpirV);
	vk::PipelineShaderStageCreateInfo FragStageCreateInfo;
	FragStageCreateInfo.stage = vk::ShaderStageFlagBits::eFragment;
	FragStageCreateInfo.module = FragModule;
	FragStageCreateInfo.pName = "main";

	SpvReflectShaderModule FragmentShaderReflection;
	SPV_REFLECT_ASSERT(spvReflectCreateShaderModule(FragmentSpirV.size(), FragmentSpirV.data(), &FragmentShaderReflection));

	vk::PipelineShaderStageCreateInfo ShaderStages[] = {VertStageCreateInfo, FragStageCreateInfo};

	//Actually hook up Shader Stages
	CreateInfo.stageCount = 2;
	CreateInfo.pStages = ShaderStages;

	std::map<uint32_t, vk::DescriptorSetLayoutBinding> DescriptorBindingsMap;
	
	//Lambda for building up list of descriptors using its ReflectionModule and ShaderStage
	auto AddDescriptorBindingsFromShaderStage = [&] (const SpvReflectShaderModule* ShaderReflectionModule, const vk::ShaderStageFlagBits ShaderStage)
	{
		uint32_t DescriptorBindingCount = 0;
		SPV_REFLECT_ASSERT(spvReflectEnumerateDescriptorBindings(ShaderReflectionModule, &DescriptorBindingCount, nullptr));

		std::vector<SpvReflectDescriptorBinding*> ReflectionDescriptorBindings(DescriptorBindingCount);
		
		if (DescriptorBindingCount > 0)
		{
			SPV_REFLECT_ASSERT(spvReflectEnumerateDescriptorBindings(ShaderReflectionModule, &DescriptorBindingCount, ReflectionDescriptorBindings.data()));

			//Sort by binding so we can easily check if the binding has already been added by a previous shader stage
			std::sort(std::begin(ReflectionDescriptorBindings), std::end(ReflectionDescriptorBindings),
			[](const SpvReflectDescriptorBinding* a, const SpvReflectDescriptorBinding* b) 
			{
				return a->binding < b->binding; 
			});

			for (auto& ReflectionDescriptorBinding : ReflectionDescriptorBindings)
			{
					auto& ExistingBinding = DescriptorBindingsMap.find(ReflectionDescriptorBinding->binding);
					if (ExistingBinding != DescriptorBindingsMap.end())
					{
						//If this binding already exists (from a previous shader stage), append this ShaderStages flag to it
						ExistingBinding->second.stageFlags |= ShaderStage;
					}
					else //Otherwise a new binding needs to be added
					{
						vk::DescriptorSetLayoutBinding DescriptorBinding = {0};
						DescriptorBinding.binding = ReflectionDescriptorBinding->binding;
						DescriptorBinding.descriptorType = (vk::DescriptorType)ReflectionDescriptorBinding->descriptor_type;
						DescriptorBinding.descriptorCount = 1; //TODO:
						DescriptorBinding.stageFlags = ShaderStage;

						DescriptorBindingsMap.emplace(DescriptorBinding.binding, DescriptorBinding);
					}
			}
		}
	};

	AddDescriptorBindingsFromShaderStage(&VertexShaderReflection,   vk::ShaderStageFlagBits::eVertex);
	AddDescriptorBindingsFromShaderStage(&FragmentShaderReflection, vk::ShaderStageFlagBits::eFragment);
	//TODO: Ability to optionally add additonal Shader Stages

	DescriptorBindings.clear();
	for (auto& Element : DescriptorBindingsMap)
	{
		DescriptorBindings.push_back(Element.second);
	}

	vk::DescriptorSetLayoutCreateInfo DescriptorLayoutCreateInfo;
	DescriptorLayoutCreateInfo.bindingCount = static_cast<uint32_t>(DescriptorBindings.size());
	DescriptorLayoutCreateInfo.pBindings = DescriptorBindings.data();

	/** build our descriptor set layout from the above descriptor set reflection data */
	DescriptorSetLayout = VulkanContext::Get()->GetDevice().createDescriptorSetLayoutUnique(DescriptorLayoutCreateInfo);

	PipelineLayoutCreateInfo.setLayoutCount = 1;
	PipelineLayoutCreateInfo.pSetLayouts = &(DescriptorSetLayout.get());

	//Fixed function create infos (these member structs can be set before running "Build Pipeline")
	CreateInfo.pVertexInputState = &VertexInput;
	CreateInfo.pInputAssemblyState = &InputAssembly;
	CreateInfo.pTessellationState = &Tessellation;

	ViewportState.viewportCount = 1;
	ViewportState.pViewports = &Viewport;
	ViewportState.scissorCount = 1;
	ViewportState.pScissors = &Scissor;
	CreateInfo.pViewportState = &ViewportState;

	CreateInfo.pRasterizationState = &Rasterizer;
	CreateInfo.pMultisampleState = &Multisampling;
	CreateInfo.pDepthStencilState = &DepthStencil;
	CreateInfo.pColorBlendState = &ColorBlending;

	//Add Dynamic State if array is nonempty
	DynamicState.dynamicStateCount = (uint32_t) DynamicStates.size();
	DynamicState.pDynamicStates = DynamicStates.data();
	CreateInfo.pDynamicState = (DynamicStates.size() > 0) ? &DynamicState : nullptr;

	//Pipeline Layout
	PipelineLayout = VulkanContext::Get()->GetDevice().createPipelineLayoutUnique(PipelineLayoutCreateInfo);
	CreateInfo.layout = PipelineLayout.get();
	
	//TODO: Hook up Layout, Renderpass, and subpass vars in struct
	//Render pass hookup
	CreateInfo.renderPass = RenderPass.GetHandle();
	CreateInfo.subpass = 0;
	
	GraphicsPipeline = VulkanContext::Get()->GetDevice().createGraphicsPipelineUnique(vk::PipelineCache(), CreateInfo);

	//Done with shader modules
	VulkanContext::Get()->GetDevice().destroyShaderModule(VertModule);
	VulkanContext::Get()->GetDevice().destroyShaderModule(FragModule);

	spvReflectDestroyShaderModule(&VertexShaderReflection);
	spvReflectDestroyShaderModule(&FragmentShaderReflection);
}

DescriptorData VulkanGraphicsPipeline::AllocateDescriptorSets(uint32_t NumSets)
{
	DescriptorData NewDescriptorData;

	NewDescriptorData.Pool = CreateDescriptorPool(NumSets);

	vk::DescriptorSetAllocateInfo DescriptorSetAllocInfo;
	DescriptorSetAllocInfo.descriptorPool = NewDescriptorData.Pool.get();
	DescriptorSetAllocInfo.descriptorSetCount = NumSets;
	DescriptorSetAllocInfo.pSetLayouts = &(DescriptorSetLayout.get());

	NewDescriptorData.Sets = VulkanContext::Get()->GetDevice().allocateDescriptorSetsUnique(DescriptorSetAllocInfo);

	return NewDescriptorData;
}

vk::UniqueDescriptorPool VulkanGraphicsPipeline::CreateDescriptorPool(uint32_t MaxSets)
{
	std::vector<vk::DescriptorPoolSize> PoolSizes = {};
	for (auto& DescriptorBinding : DescriptorBindings)
	{
		vk::DescriptorPoolSize PoolSize;
		PoolSize.type = DescriptorBinding.descriptorType;
		PoolSize.descriptorCount = DescriptorBinding.descriptorCount;
		PoolSizes.push_back(PoolSize);
	}

	vk::DescriptorPoolCreateInfo PoolCreateInfo;
	PoolCreateInfo.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
	PoolCreateInfo.pPoolSizes = PoolSizes.data();
	PoolCreateInfo.maxSets = MaxSets;
	PoolCreateInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

	return VulkanContext::Get()->GetDevice().createDescriptorPoolUnique(PoolCreateInfo);
}

std::vector<char> VulkanGraphicsPipeline::LoadShaderFromFile(const std::string& filename)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) 
	{
		std::cout << "Failed to load shader: " << filename << std::endl;
		throw std::runtime_error("failed to open file: " + filename);
    }

	size_t fileSize = (size_t) file.tellg();
	std::vector<char> code(fileSize);
	
	file.seekg(0);
	file.read(code.data(), fileSize);

	file.close();

	return code;
}

vk::ShaderModule VulkanGraphicsPipeline::CreateShaderModule(std::vector<char> spvCode)
{
	vk::ShaderModuleCreateInfo CreateInfo;
	CreateInfo.codeSize = spvCode.size();
	CreateInfo.pCode = reinterpret_cast<const uint32_t*>(spvCode.data());

	return VulkanContext::Get()->GetDevice().createShaderModule(CreateInfo);
}

void VulkanGraphicsPipeline::SetVertexInputBindings(std::vector<vk::VertexInputBindingDescription>& InputBindings, std::vector<vk::VertexInputAttributeDescription>& AttributeBindings)
{
	//Store this information so our pointers are guaranteed valid below
	VertexInputBindings = InputBindings;
	VertexAttributeBindings = AttributeBindings;

	VertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(VertexInputBindings.size());
	VertexInput.pVertexBindingDescriptions = VertexInputBindings.data();

	VertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(VertexAttributeBindings.size());
	VertexInput.pVertexAttributeDescriptions = VertexAttributeBindings.data();
}