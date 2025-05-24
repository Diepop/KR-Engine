#pragma once
#include "Utils.hpp"

namespace Kaey::Engine
{
    struct KaeyEngine
    {
        KaeyEngine(size_t threadCount = std::thread::hardware_concurrency());

        KaeyEngine(const KaeyEngine&) = delete;
        KaeyEngine(KaeyEngine&&) = delete;

        KaeyEngine& operator=(const KaeyEngine&) = delete;
        KaeyEngine& operator=(KaeyEngine&&) = delete;

        ~KaeyEngine();

        void Update();

        void SubmitSyncronized(function<void()> fn);

        KAEY_ENGINE_GETTER(Engine::ThreadPool*, ThreadPool) { return threadPool.get(); }
        KAEY_ENGINE_GETTER(Engine::RenderEngine*, RenderEngine) { return renderEngine.get(); }
        KAEY_ENGINE_GETTER(Engine::Time*, Time) { return time.get(); }
        KAEY_ENGINE_GETTER(json&, Config) { return config; }
        KAEY_ENGINE_GETTER(const fs::path&, ConfigPath) { return configPath; }
        KAEY_ENGINE_GETTER(const fs::path&, ProjectsPath) { return projectsPath; }
        KAEY_ENGINE_GETTER(const fs::path&, ShaderPath) { return shaderPath; }

    private:
        unique_ptr<Engine::ThreadPool> threadPool;
        unique_ptr<Engine::RenderEngine> renderEngine;
        unique_ptr<Engine::Time> time;
        mutable json config;
        fs::path configPath;
        fs::path projectsPath;
        fs::path shaderPath;
        mutex syncMutex;
        vector<function<void()>> syncFns;
    };

    struct RenderEngine
    {
        RenderEngine(KaeyEngine* engine, bool debugEnabled = false);

        RenderEngine(const RenderEngine&) = delete;
        RenderEngine(RenderEngine&&) noexcept = delete;

        RenderEngine& operator=(const RenderEngine&) = delete;
        RenderEngine& operator=(RenderEngine&&) noexcept = delete;

        ~RenderEngine() = default;

        RenderDevice* GetRenderDevices(i32 i) const;

        KAEY_ENGINE_GETTER(KaeyEngine*, Engine) { return engine; }
        KAEY_ENGINE_GETTER(Engine::ThreadPool*, ThreadPool) { return Engine->ThreadPool; }
        KAEY_ENGINE_GETTER(Engine::Time*, Time) { return Engine->Time; }
        KAEY_ENGINE_GETTER(vk::Instance, Instance) { return instance.get(); }
        KAEY_ENGINE_GETTER(cspan<vk::PhysicalDevice>, PhysicalDevices) { return devices; }
        KAEY_ENGINE_READONLY_ARRAY_PROPERTY(RenderDevice*, RenderDevices);
    private:
        KaeyEngine* engine;
        vk::UniqueInstance instance;
        vector<vk::PhysicalDevice> devices;
        vk::UniqueHandle<vk::DebugUtilsMessengerEXT, vk::DispatchLoaderStatic> debug;
        mutable vector<unique_ptr<RenderDevice>> renderDevices;
    };

    struct MemoryBuffer
    {
        enum MapType
        {
            Write,
            Read,
        };

        struct MapMemoryArgs
        {
            MapType Type = Write;
            Frame* frame = nullptr;
            u64 Offset = 0;
            u64 Size = 0;
        };

        struct CopyArgs
        {
            u64 SrcOffset = 0;
            u64 DstOffset = 0;
            u64 Size = 0;
            vk::CommandBuffer CommandBuffer = nullptr;
        };

        MemoryBuffer(RenderDevice* renderDevice, u64 size, vk::BufferUsageFlags usageFlags, bool deviceLocal = true);

        MemoryBuffer(const MemoryBuffer&) = delete;
        MemoryBuffer(MemoryBuffer&&) = delete;

        MemoryBuffer& operator=(const MemoryBuffer&) = delete;
        MemoryBuffer& operator=(MemoryBuffer&&) = delete;

        ~MemoryBuffer() = default;

        void MapMemory(void(*fn)(void*, void*), void* data, const MapMemoryArgs& args = {});

        template<class T, class Fn>
        void MapMemory(Fn&& fn, const MapMemoryArgs& args = {})
        {
            return MapMemory(+[](void* data, void* f) { (*(Fn*)f)((T*)data); }, &fn, args);
        }

        static void Copy(const MemoryBuffer* dst, const MemoryBuffer* src, const CopyArgs& args = {});

        KAEY_ENGINE_GETTER(vk::Buffer, Instance) { return buffer.get(); }
        KAEY_ENGINE_GETTER(vk::DeviceMemory, Memory) { return memory.get(); }
        KAEY_ENGINE_GETTER(u64, Size) { return size; }

    private:
        RenderDevice* renderDevice;
        u64 size;
        bool deviceLocal;
        vk::BufferUsageFlags usageFlags;
        vk::UniqueBuffer buffer;
        vk::UniqueDeviceMemory memory;
    };

    template<class T>
    struct DefinedMemoryBuffer : MemoryBuffer
    {
        DefinedMemoryBuffer(RenderDevice* renderDevice, u64 count, vk::BufferUsageFlags usageFlags, bool deviceLocal = true) :
            MemoryBuffer(renderDevice, count * sizeof T, usageFlags, deviceLocal)
        {
            
        }

        DefinedMemoryBuffer(RenderDevice* renderDevice, cspan<T> data, vk::BufferUsageFlags usageFlags, bool deviceLocal = true) :
            DefinedMemoryBuffer(renderDevice, data.size(), usageFlags, deviceLocal)
        {
            WriteData(data);
        }

        template<class Fn>
        auto MapMemory(Fn&& fn, MapMemoryArgs args = {})
        {
            return MemoryBuffer::MapMemory<T>([fn, count = args.Size > 0 ? args.Size : Count - args.Offset](T* ptr)
            {
                return fn(span(ptr, count));
            },
            {
                .Type = args.Type,
                .Cmd = args.Cmd,
                .Offset = args.Offset * sizeof T,
                .Size = args.Size * sizeof T,
            });
        }

        vector<T> ReadData(Frame* frame = nullptr)
        {
            auto result = vector<T>(Count);
            MapMemory([&](span<T> data) { rn::copy(data, result.begin()); }, { .Type = Read, .Cmd = cmd });
            return result;
        }

        template<rn::range Range>
        auto WriteData(Range&& range, Frame* frame = nullptr)
        {
            if constexpr (requires { rn::empty(range); })
            if (rn::empty(range)) return;
            return MapMemory([&](span<T> data) { rn::copy(range, data.begin()); }, { .Cmd = cmd });
        }

        KAEY_ENGINE_GETTER(u64, Count) { return Size / sizeof T; }

    };

    struct Frame
    {
        Frame(RenderDevice* renderDevice);

        Frame(const Frame&) = delete;
        Frame(Frame&&) noexcept = default;

        Frame& operator=(const Frame&) = delete;
        Frame& operator=(Frame&&) noexcept = default;

        ~Frame() = default;

        void BeginRender(Texture* color, Texture* depth);

        void BindPipeline(GraphicsPipeline* pipeline);

        void EndRender();

        KAEY_ENGINE_GETTER(vk::CommandBuffer, CommandBuffer) { return commandBuffer.get(); }

    private:
        RenderDevice* renderDevice;
        vk::Device device;

        vk::UniqueCommandPool commandPool;
        vk::UniqueCommandBuffer commandBuffer;


        vk::UniqueFramebuffer frameBuffer;
        vk::Extent2D lastExtent;

        Texture* color;
        Texture* depth;
        unique_ptr<DeviceQueue> renderQueue;
        GraphicsPipeline* currentPipeline;
    };

    struct DeviceQueue
    {
        DeviceQueue(RenderDevice* renderDevice, u32 familyIndex, u32 index);

        DeviceQueue(const DeviceQueue&) = delete;
        DeviceQueue(DeviceQueue&&) noexcept = delete;

        DeviceQueue& operator=(const DeviceQueue&) = delete;
        DeviceQueue& operator=(DeviceQueue&&) noexcept = delete;

        ~DeviceQueue() = default;

        void Submit(const vector<vk::CommandBuffer>& cmds);
        
        KAEY_ENGINE_GETTER(RenderDevice*, Device) { return renderDevice; }
        KAEY_ENGINE_GETTER(u32, FamilyIndex) { return familyIndex; }
        KAEY_ENGINE_GETTER(u32, Index) { return index; }
        KAEY_ENGINE_GETTER(const vk::QueueFamilyProperties&, Properties) { return properties; }
        KAEY_ENGINE_GETTER(vk::Queue, Instance) { return queue; }
        KAEY_ENGINE_GETTER(vk::CommandPool, CommandPool) { return commandPool.get(); }
        KAEY_ENGINE_GETTER(vk::CommandBuffer, CommandBuffer) { return commandBuffer.get(); }
        KAEY_ENGINE_GETTER(vk::Fence, Fence) { return fence.get(); }

    private:
        RenderDevice* renderDevice;
        u32 familyIndex, index;
        vk::Queue queue;
        vk::QueueFamilyProperties properties;
        vk::UniqueCommandPool commandPool;
        vk::UniqueCommandBuffer commandBuffer;
        vk::UniqueFence fence;
    };

    struct RenderDevice
    {
        RenderDevice(RenderEngine* renderEngine, vk::PhysicalDevice physicalDevice);

        RenderDevice(const RenderDevice&) = delete;
        RenderDevice(RenderDevice&&) noexcept = delete;

        RenderDevice& operator=(const RenderDevice&) = delete;
        RenderDevice& operator=(RenderDevice&&) noexcept = delete;

        ~RenderDevice();

        template<class T>
        auto AllocateMemory(u32 count, vk::BufferUsageFlags flags, bool deviceLocal = true)
        {
            if constexpr (std::is_void_v<T>)
                return make_unique<MemoryBuffer>(this, count, flags, deviceLocal);
            else return make_unique<DefinedMemoryBuffer<T>>(this, count, flags, deviceLocal);
        }

        void ExecuteSingleTimeCommands(const function<void(vk::CommandBuffer)>& fn, u32 familyIndex = 0);

        unique_ptr<DeviceQueue> AcquireQueue(u32 familyIndex);

        void ReleaseQueue(unique_ptr<DeviceQueue> queue);

        u32 AllocateAttribute(u32 count);
        void DeallocateAttribute(u32 index);

        KAEY_ENGINE_GETTER(KaeyEngine*, Engine) { return renderEngine->Engine; }
        KAEY_ENGINE_GETTER(Engine::RenderEngine*, RenderEngine) { return renderEngine; }
        KAEY_ENGINE_GETTER(Engine::ThreadPool*, ThreadPool) { return Engine->ThreadPool; }
        KAEY_ENGINE_GETTER(Engine::Time*, Time) { return Engine->Time; }

        KAEY_ENGINE_GETTER(vk::PhysicalDevice, PhysicalDevice) { return physicalDevice; }
        KAEY_ENGINE_GETTER(vk::Device, Instance) { return device.get(); }
        KAEY_ENGINE_GETTER(vk::DescriptorPool, DescriptorPool) { return descriptorPool.get(); }
        KAEY_ENGINE_GETTER(vk::RenderPass, RenderPass) { return renderPass.get(); }

        KAEY_ENGINE_GETTER(DefinedMemoryBuffer<Vertex>*, VertexBuffer) { return &vertexBuffer; }
        KAEY_ENGINE_GETTER(DefinedMemoryBuffer<u32>*, IndexBuffer) { return &indexBuffer; }
        KAEY_ENGINE_GETTER(DefinedMemoryBuffer<Vector4>*, AttributeBuffer) { return &attributeBuffer; }

        KAEY_ENGINE_GETTER(Engine::DiffusePipeline*, DiffusePipeline) { return diffusePipeline.get(); }

        KAEY_ENGINE_GETTER(ComputePipeline*, BindPipeline) { return bindPipeline.get(); }
        KAEY_ENGINE_GETTER(ComputePipeline*, CalcFaceTBNPipeline) { return calcFaceTBNPipeline.get(); }
        KAEY_ENGINE_GETTER(ComputePipeline*, CalcVertexTBNPipeline) { return calcVertexTBNPipeline.get(); }
        KAEY_ENGINE_GETTER(ComputePipeline*, DisplacePipeline) { return displacePipeline.get(); }
        KAEY_ENGINE_GETTER(ComputePipeline*, ShapeKeysPipeline) { return shapeKeysPipeline.get(); }
        KAEY_ENGINE_GETTER(ComputePipeline*, SurfaceDeformPipeline) { return surfaceDeformPipeline.get(); }

    private:
        Engine::RenderEngine* renderEngine;
        vk::PhysicalDevice physicalDevice;
        vk::UniqueDevice device;
        vk::UniqueDescriptorPool descriptorPool;

        struct Queue
        {
            semaphore Semaphore;
            mutex Mutex;
            vector<unique_ptr<DeviceQueue>> Queues;
        };

        vector<unique_ptr<Queue>> deviceQueues;

        vk::UniqueRenderPass renderPass;

        mutable DefinedMemoryBuffer<Vertex> vertexBuffer;
        mutable DefinedMemoryBuffer<u32> indexBuffer;
        mutable DefinedMemoryBuffer<Vector4> attributeBuffer;

        map<u32, u32> attributeMap;
        mutex attributeMutex;

        unique_ptr<Engine::DiffusePipeline> diffusePipeline;

        unique_ptr<ComputePipeline> bindPipeline;
        unique_ptr<ComputePipeline> calcFaceTBNPipeline;
        unique_ptr<ComputePipeline> calcVertexTBNPipeline;
        unique_ptr<ComputePipeline> displacePipeline;
        unique_ptr<ComputePipeline> shapeKeysPipeline;
        unique_ptr<ComputePipeline> surfaceDeformPipeline;

    };

    struct Project
    {
        Project(Engine::RenderDevice* renderDevice, fs::path rootPath);

        Project(const Project&) = delete;
        Project(Project&&) = delete;

        Project& operator=(const Project&) = delete;
        Project& operator=(Project&&) = delete;

        ~Project() noexcept = default;

        void Update();

        KAEY_ENGINE_ASSET_MAP(Texture, textureMap);
        KAEY_ENGINE_ASSET_MAP(Material, materialMap);
        KAEY_ENGINE_ASSET_MAP(MeshData, meshMap);

        KAEY_ENGINE_GETTER(Engine::RenderDevice*, RenderDevice) { return renderDevice; }
        KAEY_ENGINE_GETTER(KaeyEngine*, Engine) { return RenderDevice->Engine; }
        KAEY_ENGINE_GETTER(Engine::RenderEngine*, RenderEngine) { return RenderDevice->RenderEngine; }
        KAEY_ENGINE_GETTER(Engine::ThreadPool*, ThreadPool) { return Engine->ThreadPool; }
        KAEY_ENGINE_GETTER(Engine::Time*, Time) { return Engine->Time; }

        KAEY_ENGINE_GETTER(const fs::path&, RootPath) { return rootPath; }

        KAEY_ENGINE_GETTER(cspan<Kaey::Engine::Texture*>, Textures) { return textureMap.Assets; }
        KAEY_ENGINE_GETTER(cspan<Kaey::Engine::Material*>, Materials) { return materialMap.Assets; }
        KAEY_ENGINE_GETTER(cspan<Kaey::Engine::MeshData*>, Meshes) { return meshMap.Assets; }

    private:
        Engine::RenderDevice* renderDevice;
        fs::path rootPath;

        AssetMap<MeshData> meshMap;
        AssetMap<Material> materialMap;
        AssetMap<Texture> textureMap;
    };

}
