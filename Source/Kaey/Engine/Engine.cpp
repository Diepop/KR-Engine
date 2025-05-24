#include "Engine.hpp"
#include "All.hpp"

namespace Engine
{
    namespace
    {
        u32 FindMainQueueIndex(vk::PhysicalDevice device, vk::SurfaceKHR surface)
        {
            auto props = device.getQueueFamilyProperties();
            for (u32 i = 0; i < props.size(); ++i)
            {
                if (device.getSurfaceSupportKHR(i, surface) &&
                    props[i].queueFlags & vk::QueueFlagBits::eGraphics)
                    return i;
            }
            throw runtime_error("Failed to find family index");
        }

        struct GLSLIncluder : shaderc::CompileOptions::IncluderInterface
        {
            struct Data
            {
                string Filename;
                string Content;
            };

            fs::path ShaderPath;
            
            GLSLIncluder(fs::path shaderPath) : ShaderPath(move(shaderPath))
            {

            }

            shaderc_include_result* GetInclude(const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth) override
            {
                auto ptr = new shaderc_include_result;
                auto filePath = ShaderPath / requested_source;
                auto file = ifstream(filePath);
                assert(file.is_open());
                auto ss = stringstream();
                file >> ss.rdbuf();
                auto data = new Data{ filePath.string(), ss.str() };
                ptr->source_name = data->Filename.data();
                ptr->source_name_length = data->Filename.size();
                ptr->content = data->Content.data();
                ptr->content_length = data->Content.size();
                ptr->user_data = data;
                return ptr;
            }

            void ReleaseInclude(shaderc_include_result* data) override
            {
                delete (Data*)data->user_data;
                delete data;
            }

        };

    }

    KaeyEngine::KaeyEngine(size_t threadCount) :
        threadPool(make_unique<Engine::ThreadPool>(threadCount)),
        renderEngine(make_unique<Engine::RenderEngine>(this, IsDebug)),
        time(make_unique<Engine::Time>()),
        configPath(fs::temp_directory_path().parent_path().parent_path() / "Kaey Engine"),
        projectsPath(configPath / "Projects"),
        shaderPath("Shaders")
    {
        if (!glfwInit())
            throw runtime_error("Failed to initialize glfw!");
        if (!exists(configPath))
            create_directories(configPath);
        auto f = ifstream(configPath / "config.json");
        if (f.is_open())
            f >> config;
        options.SetIncluder(make_unique<GLSLIncluder>(shaderPath));
    }

    KaeyEngine::~KaeyEngine()
    {
        glfwTerminate();
        auto f = ofstream(configPath / "config.json");
        f << config;
    }

    void KaeyEngine::Update()
    {
        decltype(syncFns) fns;
        {
            auto l = lock_guard(syncMutex);
            fns = move(syncFns);
        }
        glfwPollEvents();
        time->Update();
        for (auto& fn : fns)
            fn();
    }

    void KaeyEngine::SubmitSyncronized(function<void()> fn)
    {
        auto l = lock_guard(syncMutex);
        syncFns.emplace_back(move(fn));
    }

    RenderEngine::RenderEngine(KaeyEngine* engine, bool debugEnabled) :
        engine(engine),
        instance([=]
        {
            if (!glfwInit())
                throw runtime_error("Failed to initialize glfw!");
            vk::ApplicationInfo appInfo{ "Vulkan Test", VK_MAKE_VERSION(1, 0, 0), "Kaey Engine", VK_MAKE_VERSION(1, 0, 0) };
            vector validationLayers{ "VK_LAYER_KHRONOS_validation" };
            u32 count;
            auto req = glfwGetRequiredInstanceExtensions(&count);
            auto ext = vector<const char*>{ req, req + count };
            if (debugEnabled)
                ext.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            ext.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            auto ptr = vk::createInstanceUnique({ { }, &appInfo, validationLayers, ext });
            if (!ptr)
                throw runtime_error("Failed to create instance!");
            return ptr;
        }()),
        devices([this]
        {
            u32 count;
            CantFail(instance->enumeratePhysicalDevices(&count, nullptr), "Failed to enumerate devices!");
            if (count == 0)
                throw runtime_error("No device found!");
            auto v = decltype(devices)(count);
            CantFail(instance->enumeratePhysicalDevices(&count, v.data()), "Failed to enumerate devices!");
            return v;
        }()),
        renderDevices(devices.size())
    {
        using Severity = vk::DebugUtilsMessageSeverityFlagBitsEXT;
        using MsgType = vk::DebugUtilsMessageTypeFlagBitsEXT;
        if (debugEnabled)
            debug = instance->createDebugUtilsMessengerEXTUnique({
                {},
                Severity::eVerbose | Severity::eWarning | Severity::eError,
                MsgType::eGeneral | MsgType::eValidation,
                +[](VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void*) -> VkBool32
                {
                    if (std::string_view(pCallbackData->pMessage).rfind("Device Extension:") == 0)
                        return VK_FALSE;
                    if (std::string_view(pCallbackData->pMessage).find("%TextureIndices = OpTypeStruct %_arr_uint_uint_") != std::string_view::npos)
                        return VK_FALSE;
                    std::cerr << pCallbackData->pMessage << "\n\n";
                    return VK_FALSE;
                }
            });
    }

    RenderDevice* RenderEngine::GetRenderDevices(i32 i) const
    {
        if (auto& ptr = renderDevices[i])
            return ptr.get();
        renderDevices[i] = make_unique<RenderDevice>(const_cast<RenderEngine*>(this), devices[i]);
        return renderDevices[i].get();
    }

    MemoryBuffer::MemoryBuffer(RenderDevice* renderDevice, u64 size, vk::BufferUsageFlags usageFlags, bool deviceLocal) :
        renderDevice(renderDevice), size(size),
        deviceLocal(deviceLocal), usageFlags(usageFlags |= vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc),
        buffer(size > 0 ? renderDevice->Instance.createBufferUnique({ {}, size, usageFlags }) : (decltype(buffer))nullptr),
        memory([&]
        {
            if (size == 0)
                return (decltype(memory))nullptr;
            auto dev = renderDevice->Instance;
            auto r = dev.getBufferMemoryRequirements(*buffer);
            auto mem = dev.allocateMemoryUnique({
                r.size,
                FindMemoryIndex(renderDevice->PhysicalDevice, r.memoryTypeBits,
                deviceLocal ? vk::MemoryPropertyFlagBits::eDeviceLocal : vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible)
            });
            dev.bindBufferMemory(*buffer, *mem, 0);
            return mem;
        }())
    {

    }

    void MemoryBuffer::MapMemory(void(*fn)(void*, void*), void* data, const MapMemoryArgs& args)
    {
        auto dev = renderDevice->Instance;
        auto& [type, cmd, offset, sz] = args;
        auto size = sz > 0 ? sz : this->size - offset;
        if (!deviceLocal)
        {
            fn(dev.mapMemory(memory.get(), offset, size), data);
            dev.unmapMemory(memory.get());
            return;
        }

        MemoryBuffer tmpBuf{ renderDevice, size, usageFlags | vk::BufferUsageFlagBits::eStorageBuffer, false };

        if (type == Write)
        {
            fn(dev.mapMemory(tmpBuf.Memory, 0, size), data);
            dev.unmapMemory(tmpBuf.Memory);
        }

        auto ffn = [&](vk::CommandBuffer c)
        {
            vk::BufferCopy region
            {
                type == Write ? 0 : offset, //src
                type == Read ? 0 : offset, //dst
                size
            };
            if (type == Write)
                c.copyBuffer(tmpBuf.Instance, buffer.get(), 1, &region);
            else c.copyBuffer(buffer.get(), tmpBuf.Instance, 1, &region);
        };

        if (cmd)
            ffn(cmd);
        else renderDevice->ExecuteSingleTimeCommands(ffn);

        if (type == Read)
        {
            fn(dev.mapMemory(tmpBuf.Memory, 0, size), data);
            dev.unmapMemory(tmpBuf.Memory);
        }
    }

    void MemoryBuffer::Copy(const MemoryBuffer* dst, const MemoryBuffer* src, const CopyArgs& args)
    {
        assert(src->renderDevice == dst->renderDevice);
        auto fn = [=](vk::CommandBuffer cmd)
        {
            vk::BufferCopy copy
            {
                args.SrcOffset,
                args.DstOffset,
                args.Size == 0 ? src->Size - args.SrcOffset : args.Size
            };
            copy.size = std::min(copy.size, dst->Size - args.DstOffset);
            cmd.copyBuffer(src->Instance, dst->Instance, copy);
        };
        if (!args.CommandBuffer)
            src->renderDevice->ExecuteSingleTimeCommands(fn);
        else fn(args.CommandBuffer);
    }
    
    Frame::Frame(RenderDevice* renderDevice) :
        renderDevice(renderDevice), device(renderDevice->Instance),
        commandPool(renderDevice->Instance.createCommandPoolUnique({ vk::CommandPoolCreateFlagBits::eResetCommandBuffer })),
        commandBuffer(move(renderDevice->Instance.allocateCommandBuffersUnique({ commandPool.get(), vk::CommandBufferLevel::ePrimary, 1 }).front())),
        lastExtent({ 0, 0 }),
        color(nullptr),
        depth(nullptr),
        currentPipeline(nullptr)
    {

    }

    void Frame::BeginRender(Texture* color, Texture* depth)
    {
        assert(color->Extent == depth->Extent);
        renderQueue = renderDevice->AcquireQueue(0);
        this->color = color;
        this->depth = depth;
        auto cmd = CommandBuffer;
        cmd.reset();
        cmd.begin({ {}, nullptr });

        if (!frameBuffer || lastExtent != color->Extent)
        {
            auto& [w, h] = lastExtent = color->Extent;
            auto views = vector{ color->ImageView, depth->ImageView };
            frameBuffer = device.createFramebufferUnique({ {}, renderDevice->RenderPass, views, w, h, 1 });
        }

        vk::ClearValue clearColors[2];
        clearColors[0].color.float32[0] = 0;
        clearColors[0].color.float32[1] = 0;
        clearColors[0].color.float32[2] = 0;
        clearColors[0].color.float32[3] = 1;
        clearColors[1].color.float32[0] = 1;
        cmd.beginRenderPass({ renderDevice->RenderPass, frameBuffer.get(), { { 0, 0 }, color->Extent }, 2, clearColors }, vk::SubpassContents::eInline);
    }

    void Frame::BindPipeline(GraphicsPipeline* pipeline)
    {
        assert(pipeline != nullptr);
        if (pipeline == currentPipeline)
            return;
        pipeline->OnBind(this, color);
        currentPipeline = pipeline;
    }

    void Frame::EndRender()
    {
        auto cmd = CommandBuffer;
        cmd.endRenderPass();
        cmd.end();
        currentPipeline = nullptr;
        renderQueue->Submit({ cmd });
        renderDevice->ReleaseQueue(move(renderQueue));
    }

    Swapchain::Swapchain(Window* window, RenderDevice* renderDevice, u32 maxFrames) :
        window(window), renderDevice(renderDevice), maxFrames(maxFrames),
        imageAvaliableFence(renderDevice->Instance.createFenceUnique({ })),
        frameCount(0),
        queue(renderDevice->AcquireQueue(FindMainQueueIndex(renderDevice->PhysicalDevice, window->Surface)))
    {
        Recreate();
        window->AddFramebufferSizeCallback([this](Window*, int, int) { framebufferResized = true; });
    }

    void Swapchain::Present(Texture* tex)
    {
        if (window->IsMinimized())
            return;
        if (framebufferResized)
            Recreate();
        try
        {
            auto dev = renderDevice->Instance;
            auto& fence = imageAvaliableFence.get();
            dev.resetFences(fence);
            auto imageIndex = CantFail(dev.acquireNextImageKHR(Instance, UINT64_MAX, nullptr, fence), "Failed to acquire next image!");
            
            renderDevice->ExecuteSingleTimeCommands([&](vk::CommandBuffer cmd)
            {
                auto layout = tex->Layout;
                tex->ChangeLayout(vk::ImageLayout::eTransferSrcOptimal, cmd);
                Texture::ChangeLayout(renderDevice, images[imageIndex], vk::ImageLayout::eTransferDstOptimal,
                    frameCount < maxFrames ? vk::ImageLayout::eUndefined : vk::ImageLayout::ePresentSrcKHR,
                    vk::ImageAspectFlagBits::eColor,
                    cmd);
                vk::ImageBlit b{  };
                b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
                b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = tex->AspectMask;
                b.srcOffsets[1] = b.dstOffsets[1] = vk::Offset3D{ (i32)tex->Extent.width, (i32)tex->Extent.height, 1 };
                cmd.blitImage(tex->Instance, tex->Layout, images[imageIndex], vk::ImageLayout::eTransferDstOptimal, b, vk::Filter::eLinear);
                Texture::ChangeLayout(renderDevice, images[imageIndex], vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eTransferDstOptimal,
                    vk::ImageAspectFlagBits::eColor,
                    cmd);
                tex->ChangeLayout(layout, cmd);
            });

            auto queue = Queue->Instance;
            CantFail(dev.waitForFences(fence, true, UINT64_MAX), "Failed to wait for fence!");
            auto sc = Instance;
            CantFail(queue.presentKHR({ nullptr, sc, imageIndex }), "Failed to present swap chain image!");
        }
        catch (vk::OutOfDateKHRError&)
        {
            framebufferResized = true;
            Present(tex);
            return;
        }
        ++frameCount;
    }

    void Swapchain::Recreate()
    {
        auto surfaceFormat = window->SurfaceFormat;
        swapchain = renderDevice->Instance.createSwapchainKHRUnique({
            {},
            window->Surface,
            maxFrames,
            surfaceFormat.format,
            surfaceFormat.colorSpace,
            window->Extent,
            1,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst,
            vk::SharingMode::eExclusive,
            0, nullptr,
            vk::SurfaceTransformFlagBitsKHR::eIdentity,
            vk::CompositeAlphaFlagBitsKHR::eOpaque,
            vk::PresentModeKHR::eMailbox,
            true,
            swapchain.get()
        });
        images = renderDevice->Instance.getSwapchainImagesKHR(swapchain.get());
        imageViews = images | vs::transform([&](auto img) { return renderDevice->Instance.createImageViewUnique({ {}, img, vk::ImageViewType::e2D, surfaceFormat.format, {}, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } }); }) | to_vector;
        frameCount = 0;
        framebufferResized = false;
    }

    DeviceQueue::DeviceQueue(RenderDevice* renderDevice, u32 familyIndex, u32 index) :
        renderDevice(renderDevice), familyIndex(familyIndex), index(index),
        queue(renderDevice->Instance.getQueue(familyIndex, index)),
        properties(renderDevice->PhysicalDevice.getQueueFamilyProperties()[familyIndex]),
        commandPool(renderDevice->Instance.createCommandPoolUnique({ vk::CommandPoolCreateFlagBits::eResetCommandBuffer, familyIndex })),
        commandBuffer(move(renderDevice->Instance.allocateCommandBuffersUnique({ commandPool.get(), vk::CommandBufferLevel::ePrimary, 1 }).front())),
        fence(renderDevice->Instance.createFenceUnique({}))
    {
        
    }

    void DeviceQueue::Submit(const vector<vk::CommandBuffer>& cmds)
    {
        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = (u32)cmds.size();
        submitInfo.pCommandBuffers = cmds.data();
        auto dev = Device->Instance;
        dev.resetFences(fence.get());
        CantFail(Instance.submit(1, &submitInfo, fence.get()), "Failed to submit command!");
        CantFail(dev.waitForFences(fence.get(), true, UINT64_MAX), "Failed to wait for fence!");
    }

    RenderDevice::RenderDevice(Engine::RenderEngine* renderEngine, vk::PhysicalDevice physicalDevice) :
        renderEngine(renderEngine),
        physicalDevice(physicalDevice),
        device([&]
        {
            auto familyProperties = physicalDevice.getQueueFamilyProperties();
            auto priorities = familyProperties | vs::transform([](auto& props)
            {
                return vector((size_t)props.queueCount, 1.0f);
            }) | to_vector;
            auto queueInfos = irange((u32)familyProperties.size()) | vs::transform([&](u32 i) -> vk::DeviceQueueCreateInfo
            {
                return { {}, i, familyProperties[i].queueCount, priorities[i].data() };
            }) | to_vector;
            vector validationLayers{ "VK_LAYER_KHRONOS_validation" };
            vector extensions
            {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_KHR_UNIFORM_BUFFER_STANDARD_LAYOUT_EXTENSION_NAME,
            };
            return physicalDevice.createDeviceUnique({
                {},
                queueInfos,
                validationLayers,
                extensions,
            });
        }()),
        descriptorPool([&]
        {
            vk::DescriptorPoolSize poolSizes[] =
            {
                { vk::DescriptorType::eSampler, 1000 },
                { vk::DescriptorType::eCombinedImageSampler, 1000 },
                { vk::DescriptorType::eSampledImage, 1000 },
                { vk::DescriptorType::eStorageImage, 1000 },
                { vk::DescriptorType::eUniformTexelBuffer, 1000 },
                { vk::DescriptorType::eStorageTexelBuffer, 1000 },
                { vk::DescriptorType::eUniformBuffer, 1000 },
                { vk::DescriptorType::eStorageBuffer, 1000 },
                { vk::DescriptorType::eUniformBufferDynamic, 1000 },
                { vk::DescriptorType::eStorageBufferDynamic, 1000 },
                { vk::DescriptorType::eInputAttachment, 1000 }
            };
            return device->createDescriptorPoolUnique({ vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1000, std::size(poolSizes), poolSizes});
        }()),
        deviceQueues([&]
        {
            decltype(deviceQueues) v;
            auto props = physicalDevice.getQueueFamilyProperties();
            for (u32 family = 0; family < props.size(); ++family)
            {
                auto count = props[family].queueCount;
                auto& q = v.emplace_back(new Queue{ semaphore{ count }, {}, {} });
                for (u32 index = 0; index < count; ++index)
                    q->Queues.emplace_back(make_unique<DeviceQueue>(this, family, index));
            }
            return v;
        }()),
        renderPass([&]
        {
            vector<vk::AttachmentDescription> attachments;
            attachments.reserve(2);

            //Color attachment
            attachments.push_back({
                {},
                vk::Format::eR8G8B8A8Srgb,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eShaderReadOnlyOptimal
            });

            //Depth attachment
            attachments.push_back({
                {},
                vk::Format::eD32Sfloat,
                vk::SampleCountFlagBits::e1,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eDontCare,
                vk::AttachmentLoadOp::eDontCare,
                vk::AttachmentStoreOp::eDontCare,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eDepthStencilReadOnlyOptimal
            });

            vk::AttachmentReference colorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
            vk::AttachmentReference depthAttachmentRef{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };

            auto subpass = vk::SubpassDescription()
                .setColorAttachments(colorAttachmentRef)
                .setPDepthStencilAttachment(&depthAttachmentRef)
                ;

            auto dependency = vk::SubpassDependency()
                .setDependencyFlags(vk::DependencyFlagBits::eByRegion)
                .setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                .setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests)
                .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite)
                ;

            return device->createRenderPassUnique({
                {},
                attachments,
                subpass,
                dependency
            });
        }()),
        vertexBuffer(this, 50000, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer),
        indexBuffer(this, 50000, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer),
        attributeBuffer(this, 5000000, vk::BufferUsageFlagBits::eStorageBuffer),
        diffusePipeline(make_unique<Engine::DiffusePipeline>(this)),
        bindPipeline(make_unique<ComputePipeline>(this, LoadShaders(Instance, { { rc_bind_comp_spv, vk::ShaderStageFlagBits::eCompute } }))),
        calcFaceTBNPipeline(make_unique<ComputePipeline>(this, LoadShaders(Instance, { { rc_calc_face_tbn_comp_spv, vk::ShaderStageFlagBits::eCompute } }))),
        calcVertexTBNPipeline(make_unique<ComputePipeline>(this, LoadShaders(Instance, { { rc_calc_vertex_tbn_comp_spv, vk::ShaderStageFlagBits::eCompute } }))),
        displacePipeline(make_unique<ComputePipeline>(this, LoadShaders(Instance, { { rc_displace_comp_spv, vk::ShaderStageFlagBits::eCompute } }))),
        shapeKeysPipeline(make_unique<ComputePipeline>(this, LoadShaders(Instance, { { rc_shape_keys_comp_spv, vk::ShaderStageFlagBits::eCompute } }))),
        surfaceDeformPipeline(make_unique<ComputePipeline>(this, LoadShaders(Instance, { { rc_surface_deform_comp_spv, vk::ShaderStageFlagBits::eCompute } })))
    {

    }

    RenderDevice::~RenderDevice()
    {
        GetInstance().waitIdle();
    }

    void RenderDevice::ExecuteSingleTimeCommands(const function<void(vk::CommandBuffer)>& fn, u32 familyIndex)
    {
        struct QueueLock
        {
            unique_ptr<DeviceQueue> Queue;
            QueueLock(RenderDevice* d, u32 familyIndex) noexcept : Queue(d->AcquireQueue(familyIndex)) {  }
            ~QueueLock() noexcept { Queue->Device->ReleaseQueue(move(Queue)); }
        };

        QueueLock lock{ this, familyIndex };
        auto& queue = lock.Queue;

        auto cmd = queue->CommandBuffer;
        cmd.reset();
        cmd.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        fn(cmd);

        cmd.end();

        queue->Submit({ cmd });
    }

    unique_ptr<DeviceQueue> RenderDevice::AcquireQueue(u32 familyIndex)
    {
        unique_ptr<DeviceQueue> queue;
        auto& [sem, mut, qu] = *deviceQueues[familyIndex];
        sem.acquire();
        {
            lock_guard l{ mut };
            auto it = rn::find_if(qu, [=](auto& ptr) { return ptr && ptr->FamilyIndex == familyIndex; });
            swap(*it, queue);
        }
        return queue;
    }

    void RenderDevice::ReleaseQueue(unique_ptr<DeviceQueue> queue)
    {
        assert(queue->Device == this);
        auto& [sem, mut, qu] = *deviceQueues[queue->FamilyIndex];
        {
            lock_guard l{ mut };
            auto it = rn::find(qu, unique_ptr<DeviceQueue>());
            *it = move(queue);
        }
        sem.release();
    }

    u32 RenderDevice::AllocateAttribute(u32 count)
    {
        auto l = lock_guard(attributeMutex);
        if (attributeMap.empty())
        {
            assert(count < AttributeBuffer->Count);
            attributeMap.emplace(0, count);
            return 0;
        }
        auto& [i, c] = *attributeMap.rbegin();
        auto idx = i + c;
        assert(idx + count < AttributeBuffer->Count);
        attributeMap.emplace(idx, count);
        return idx;
    }

    void RenderDevice::DeallocateAttribute(u32 index)
    {
        assert(attributeMap.contains(index));
        attributeMap.erase(index);
    }

    Project::Project(Engine::RenderDevice* renderDevice, fs::path rootPath) :
        renderDevice(renderDevice),
        rootPath(move(rootPath))
    {
        fs::current_path(RootPath);
    }

    void Project::Update()
    {
        meshMap.Update();
        materialMap.Update();
        textureMap.Update();
    }

}
