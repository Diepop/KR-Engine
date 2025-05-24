#include "Kaey/Renderer/DynamicRenderPipeline.hpp"
#include "Kaey/Renderer/ImGui.hpp"
#include "Kaey/Renderer/Renderer.hpp"
#include "Kaey/Renderer/ThreadPool.hpp"
#include "Kaey/Renderer/Time.hpp"
#include "Kaey/Renderer/Utility.hpp"
#include "Kaey/Renderer/Window.hpp"

#include <GTAOPipeline.hpp>
#include <RenderTexPipeline.hpp>
#include <Slang/PBRPipeline.hpp>
#include <Slang/TestPipeline.hpp>
#include <Slang/OutlinePipeline.hpp>

#include "Mesh.hpp"

namespace Kaey::Renderer
{
    namespace
    {
        Matrix4 CalcProjectionMatrix(const Vector2& sSize, f32 fov, f32 far, f32 near)
        {
            auto ar = sSize.x / sSize.y;
            auto tg = std::tan(fov / 2);
            return
            {
                { 1 / (ar * tg), 0,       0,                          0, },
                { 0,            -1 / tg,  0,                          0, },
                { 0,             0,      (far + near) / (far - near), 1, },
                { 0,             0,     -(far * near) / (far - near), 0, },
            };
        }

        Matrix4 CalcViewMatrix(const Vector3& pos, const Quaternion& rot)
        {
            return Matrix4::Translation(-pos) * rot.Matrix;
        }

    }

    template<class T>
    class AllocatedObject
    {
        T* ptr;
        GPUVirtualMemoryAllocator* allocator;

    public:
        AllocatedObject(T* ptr = nullptr, GPUVirtualMemoryAllocator* allocator = nullptr) : ptr(ptr), allocator(allocator)
        {

        }

        AllocatedObject(GPUVirtualMemoryAllocator* allocator, size_t n = 1) : AllocatedObject(allocator->AllocateAddress<T>(n), allocator)
        {

        }

        KR_NO_COPY(AllocatedObject);

        AllocatedObject(AllocatedObject&& v) noexcept : AllocatedObject()
        {
            *this = std::move(v);
        }

        AllocatedObject& operator=(AllocatedObject&& v) noexcept
        {
            allocator->DeallocateAddress<T>(ptr);
            ptr = std::exchange(v.ptr, nullptr);
            allocator = v.allocator;
            return *this;
        }

        ~AllocatedObject()
        {
            allocator->DeallocateAddress<T>(ptr);
        }

        T& operator*() const { return *ptr; }
        T* operator->() const { return ptr; }

        T& operator[](auto i) const { return ptr[i]; }

        KR_GETTER(u32, Index) { return u32(ptr - (T*)allocator->MappedAddress); }

    };

}

using namespace Kaey::Renderer;
using namespace Kaey;

using enum MeshAttributeDomain;
using enum MeshAttributeType;

extern const auto Assets  = fs::path(ASSETS_PATH);
extern const auto Shaders = fs::path(SHADERS_PATH);

namespace
{
    
    vector<unique_ptr<MeshData3D>> LoadObj(const tinyobj::ObjReader& reader, SceneData* sceneData)
    {
        auto& at = reader.GetAttrib();
        auto posSpan = span{ (const Vector3*)at.vertices.data(),   at.vertices.size() / 3 };
        auto uvSpan  = span{ (const Vector2*)at.texcoords.data(), at.texcoords.size() / 2 };
        u32 vertexDelta = 0;
        auto load = [&](const tinyobj::shape_t& shape) -> unique_ptr<MeshData3D>
        {
            auto& mesh = shape.mesh;

            auto cornerCount = mesh.indices.size();

            vector<u32> pointsOfCorners; pointsOfCorners.reserve(cornerCount);
            vector<Vector2F16> uvs; uvs.reserve(cornerCount);

            u32 vertexCount = 0;
            for (auto faceIt = mesh.indices.data(); auto faceCount : mesh.num_face_vertices)
            {
                for (auto faceId : irange(faceCount))
                {
                    auto& [vIndex, nIndex, uvIndex] = faceIt[faceId];
                    auto index = pointsOfCorners.emplace_back(vIndex - vertexDelta);
                    vertexCount = std::max(vertexCount, index + 1);
                    {
                        auto uv = uvIndex != -1 ? uvSpan[uvIndex] : 0_xy;
                        float y;
                        uv.y = 1 - std::modf(uv.y, &y);
                        uv.y += y;
                        uvs.emplace_back((f16)uv.x, (f16)uv.y);
                    }
                }
                faceIt += faceCount;
            }
            vertexDelta += vertexCount;

            auto meshData = make_unique<MeshData3D>(sceneData, string(), vertexCount, (u32)cornerCount / 3, (u32)cornerCount);

            if (meshData->PointOfCorner->Type == UInt32)
                rn::copy(pointsOfCorners, meshData->PointsOfCorners32.data());
            else rn::copy(pointsOfCorners, meshData->PointsOfCorners16.data());

            rn::copy(posSpan.subspan(0, vertexCount) | vs::multiplied(-1_x + 1_y), meshData->Positions.data());
            posSpan = posSpan.subspan(vertexCount);

            auto uvAtt = meshData->AddAttribute("UVMap", Corner, Vec2F16);
            rn::copy(uvs, (Vector2F16*)uvAtt->Buffer.data());

            return meshData;
        };
        return reader.GetShapes() | vs::transform(load) | to_vector;
    }

    vector<unique_ptr<MeshData3D>> LoadObj(crpath path, SceneData* sceneData)
    {
        tinyobj::ObjReader reader;
        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        if (!reader.ParseFromFile(path.string(), config))
            throw runtime_error("!");
        return LoadObj(reader, sceneData);
    }

}

namespace
{
    template<class T>
    T saturate(T v)
    {
        return std::clamp(v, T(0), T(1));
    }

    Vector2 OctWrap(Vector2 v)
    {
        auto c = Vector2(v.x >= 0 ? 1 : -1, v.y >= 0 ? 1 : -1);
        return (1_xy - Vector2(abs(v.y), abs(v.x))) * c;
    }

    Vector2 Encode(Vector3 n)
    {
        n /= abs(n.x) + abs(n.y) + abs(n.z);
        n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
        n.xy = n.xy * 0.5 + 0.5_xy;
        return n.xy;
    }

    Vector3 Decode(Vector2 f)
    {
        f = f * 2 - 1_xy;
        auto n = Vector3(f.x, f.y, 1.f - abs(f.x) - abs(f.y));
        auto t = saturate(-n.z);
        n.x += n.x >= 0 ? -t : t;
        n.y += n.y >= 0 ? -t : t;
        return n.Normalized;
    }

    u32 EncodeU(Vector3 n)
    {
        auto [x, y] = Encode(n);
        auto lo = u32(u16(x * 65535)) <<  0;
        auto hi = u32(u16(y * 65535)) << 16;
        return lo | hi;
    }

    Vector3 DecodeU(u32 u)
    {
        auto lo = u32(u & 0x0000FFFF) >>  0;
        auto hi = u32(u & 0xFFFF0000) >> 16;
        auto x = f32(lo) / 65535;
        auto y = f32(hi) / 65535;
        return Decode({ x, y });
    }

}

int main(int argc, char* argv[])
{
    using enum vk::Format;
    try
    {
        auto renderEngine = RenderEngine();
        auto threadPool = ThreadPool();

        auto adapters = renderEngine.RenderAdapters;

        if (adapters.empty())
        {
            println("No avaliable render device found!");
            return -1;
        }

        println("Render devices:\n{}\n", join(adapters | vs::transform(&RenderAdapter::GetName), "\n"));

        auto& adapter = adapters[0];
        println("Using '{}' as render device.\n", adapter->Name);

        for (auto&& prop : adapter->Instance.getQueueFamilyProperties())
            println("Queue count:\t{:#2},\tflags:\t{}", prop.queueCount, prop.queueFlags);

        //auto memProp = adapter->Instance.getMemoryProperties();
        //for (auto i : irange(memProp.memoryHeapCount))
        //{
        //    auto& heap = memProp.memoryHeaps[i];
        //    println("Heap {}: {}mb -> {}", i, heap.size / 1_mb, heap.flags);
        //}
        //for (auto i : irange(memProp.memoryTypeCount))
        //{
        //    auto& type = memProp.memoryTypes[i];
        //    println("{}: {} {}", i, type.propertyFlags, type.heapIndex);
        //}
        //println("MaxComputeWorkGroupCount: {}", adapter->Instance.getProperties().limits.maxComputeWorkGroupCount);
        //println("MaxComputeSharedMemorySize: {}", adapter->Instance.getProperties().limits.maxComputeSharedMemorySize);
        //println("MaxComputeWorkGroupInvocations: {}", adapter->Instance.getProperties().limits.maxComputeWorkGroupInvocations);
        //println("MaxComputeWorkGroupSize: {}", adapter->Instance.getProperties().limits.maxComputeWorkGroupSize);
        //println("{}", adapter->Instance.getProperties().limits.maxComputeWorkGroupCount | vs::transform(KR_FN_OBJ(std::bit_width)));
        //{
        //    vk::PhysicalDeviceProperties2 props;
        //    vk::PhysicalDeviceSubgroupProperties subgroupProperties;
        //    props.pNext = &subgroupProperties;
        //    adapter->Instance.getProperties2(&props);
        //    println("{}", subgroupProperties.subgroupSize);
        //}

        auto deviceInst = RenderDevice(adapter);
        auto device = &deviceInst;

        auto window = Window
        {
            device,
            "Kaey Renderer",
            { 1280, 720 },
            {
                { GLFW_CLIENT_API, GLFW_NO_API },
                //{ GLFW_RESIZABLE, GLFW_FALSE },
                //{ GLFW_DECORATED, GLFW_FALSE },
                //{ GLFW_AUTO_ICONIFY, GLFW_TRUE },
            }
        };

        auto swapchain = Swapchain(&window, { .VerticalSync = false, .MaxFrames = 2, .FrameRateCap = 0 });

        auto frames
            = irange(swapchain.MaxFrames)
            | vs::transform([=](...) { return make_unique<Frame>(device); })
            | to_vector
            ;

        auto sceneData = SceneData(device);

        auto pbr = Renderer::Slang::PBRPipeline(device);

        auto op = Renderer::Slang::OutlinePipeline(device);

        using Pipeline = Renderer::Slang::TestPipeline;

        auto tp = Pipeline(device);

        tp.Culling = FaceCulling::None;

        Pipeline::UniformCamera uniformCamera;
        uniformCamera.Far  = 100;
        uniformCamera.Near = .01f;

        auto cameraPosition = .36_x + 1.6_y + -1.75_z;
        auto cameraAngle = 0_xy;
        auto camRot = [&] { return Quaternion::AngleAxis(cameraAngle.y, 1_right) * Quaternion::AngleAxis(cameraAngle.x, 1_up); };
        auto cameraRotation = camRot();
        auto updateCamera = [&]
        {
            auto& o = uniformCamera;
            o.Position    = cameraPosition;
            o.View        = CalcViewMatrix(cameraPosition, cameraRotation);
            o.Projection  = CalcProjectionMatrix((Vector2)window.Size, 90_deg, uniformCamera.Far, uniformCamera.Near);
            using Mat4 = linm::MatrixMN<f64, 4, 4>; //More precision for inverse.
            o.InvProj     = Matrix4(Mat4(o.Projection).Inverse);
            o.InvProjView = Matrix4(Mat4(o.View * o.Projection).Inverse);
        };
        auto calcNDC = [&]
        {
            auto [x, y] = (Vector2)window.Size / 2;
            return Matrix4
            {
                { x, 0, 0, 0.5, },
                { 0, y, 0, 0.5, },
                { 0, 0, 1, 0.0, },
                { 0, 0, 0, 1.0, },
            };
        };
        auto lockedCursor = 0_xy;
        window.AddCursorPosCallback([&](Window* win, const Vector2& currentPos)
        {
            if (!win->IsActive())
                return;
            auto& io = ImGui::GetIO();
            if (!io.MouseDown[1])
                return;
            auto delta = currentPos - lockedCursor;
            cameraAngle -= Matrix2::Scale2D(25_deg, 15_deg) * delta * .01f;
            cameraAngle.x = std::modf(cameraAngle.x / 2_pi, &cameraAngle.x) * 2_pi;
            cameraAngle.y = std::clamp(cameraAngle.y, -89_deg, 89_deg);
            updateCamera();
            win->SetCursorPos(lockedCursor);
        });

        static constexpr auto& TexPath = ASSETS_PATH "/Textures/Genesis 9/Characters/Naoimhe 9/Naoimhe";
        static constexpr auto& TexExt  = "jpg";
        auto futureTextures
            = irange(5)
            | vs::transform([&](auto i) { return Texture::LoadUniqueAsync(&threadPool, device, "{}D_100{}.{}"_f(TexPath, i + 1, TexExt), { .Format = eR8G8B8A8Srgb, .MaxMipLevel = 0 }); })
            | to_vector
            ;

        futureTextures.insert_range(futureTextures.end(),
            irange(5) | vs::transform([&](auto i) { return Texture::LoadUniqueAsync(&threadPool, device, "{}N_100{}.{}"_f(TexPath, i + 1, TexExt), { .Format = eR8G8B8A8Unorm, .MaxMipLevel = 0 }); })
        );

        auto sampler = Sampler(device, { .LODBias = -1.5f, .MaxAnisotropy = 16.f });

        //auto objMeshes = LoadObj(Assets / "Verity.obj", &sceneData);
        //auto objMeshes = LoadSceneFile(&sceneData, Assets / "Genesis 9 Merged None Tri.ksc");
        auto loadedScene = LoadSceneFile(&sceneData, Assets / "G9 Shapes.ksc");

        auto normalTask = threadPool.Submit(KR_FN_OBJ(device->ExecuteSingleTimeCommands), [&](Frame* frame)
        {
            auto meshes = loadedScene.MeshDatas | vs::filter([](auto& p) { return p != nullptr; }) | vs::transform(&unique_ptr<MeshData3D>::get) | to_vector;
            for (auto& m : meshes) m->Write(frame);
            frame->WaitForCommands();
            for (auto& m : meshes) m->CalcMorphs(frame);
            frame->WaitForCommands();
            for (auto& m : meshes) m->CalcFaceNormals(frame);
            for (auto& m : meshes) m->CalcUvTangents(frame);
            frame->WaitForCommands();
            for (auto& m : meshes) m->CalcPointNormals(frame);
        });

        auto stextures = futureTextures | vs::keys | vs::recast<ITexture*>() | vs::transform([&](auto tex) { return pair(tex, &sampler); }) | to_vector;

        updateCamera();

        auto colorOverride  = 1_xyzw;
        auto wireFrameWidth =   .01f;

        tp.SceneIndex     = sceneData.Index;
        tp.MeshIndex      = 0;
        tp.CameraIndex    = 0;
        tp.ColorOverride  = colorOverride;
        tp.NDCIndex       = 0;
        tp.WireFrameWidth = wireFrameWidth;

        auto screenSampler = Sampler(device, { .Interpolation = Interpolation::Nearest, .Extrapolation = Extrapolation::Clip, });
        auto rtp = RenderTexPipeline(device);

        struct TargetTextures
        {
            unique_ptr<Texture> Depth, Albm, Nrmr, Poss, Overlay, Color, Gtao, AoInter, Edge, GtaoDepth, Index;
            vector<ITexture*> PreviewTextures;
        };

        auto targetTextures = vector<TargetTextures>(frames.size());

        rtp.Far = uniformCamera.Far;
        rtp.Near = uniformCamera.Near;
        //rtp.IsDepth = true;
        rtp.TextureIndex = 0;

        window.AddFramebufferSizeCallback([&](Window*, int, int)
        {
            for (auto& v : targetTextures)
            {
                v.Depth     = make_unique<Texture>(device, TextureArgs{ window.Size, eD32Sfloat,          1 });
                v.Albm      = make_unique<Texture>(device, TextureArgs{ window.Size, eR8G8B8A8Unorm,      1 });
                v.Nrmr      = make_unique<Texture>(device, TextureArgs{ window.Size, eR8G8B8A8Unorm,      1 });
                v.Poss      = make_unique<Texture>(device, TextureArgs{ window.Size, eR16G16B16A16Sfloat, 1 });
                v.Overlay   = make_unique<Texture>(device, TextureArgs{ window.Size, eR8G8B8A8Unorm,      1 });
                v.Color     = make_unique<Texture>(device, TextureArgs{ window.Size, eR16G16B16A16Sfloat, 1 });
                v.Gtao      = make_unique<Texture>(device, TextureArgs{ window.Size, eR8Unorm,            1, 0_xyzw });
                v.AoInter   = make_unique<Texture>(device, TextureArgs{ window.Size, eR8Unorm,            1 });
                v.Edge      = make_unique<Texture>(device, TextureArgs{ window.Size, eR32Sfloat,          1 });
                v.GtaoDepth = make_unique<Texture>(device, TextureArgs{ window.Size, eR32Sfloat,          5 });
                v.Index     = make_unique<Texture>(device, TextureArgs{ window.Size, eR16Uint,            1 });
                
                v.PreviewTextures.clear();
                v.PreviewTextures.emplace_back(v.Color.get());
                v.PreviewTextures.emplace_back(v.Albm.get());
                v.PreviewTextures.emplace_back(v.Nrmr.get());
                v.PreviewTextures.emplace_back(v.Poss.get());
                v.PreviewTextures.emplace_back(v.Overlay.get());
                v.PreviewTextures.emplace_back(v.Depth.get());
                v.PreviewTextures.emplace_back(v.Gtao.get());
            }
            updateCamera();
        }, true);
        
        auto gtaoEnabled   = true;
        auto gtaoSmooth    = true;
        auto gtaoPrefilter = GTAO::PrefilterDepths16x16Pipeline(device);
        auto gtao          = GTAO::GTAOUltraPipeline(device);
        auto gtaoDenoise   = GTAO::DenoisePassPipeline(device);
        auto gtaoDenoise2  = GTAO::DenoiseLastPassPipeline(device);
        auto consts        = GTAO::GTAOConstants
        {
            .EffectRadius = .05f,
            .EffectFalloffRange = 1,
            .RadiusMultiplier = 5,
            .FinalValuePower = 1,
            .DenoiseBlurBeta = 50,
            .SampleDistributionPower = 1.5f,
            .ThinOccluderCompensation = 2,
            .DepthMIPSamplingOffset = 2,
            .NoiseIndex = 0,
        };

        auto imGui = ImGuiInstance(&window);

        sceneData.Data->AmbientLight = 0.15_xyz;

        sceneData.Data->LightCount = 3;
        sceneData.Data->LightOffset = sceneData.SceneAllocator->AllocateIndex32<Pipeline::UniformLight>(sceneData.Data->LightCount);
        auto lights = span((Pipeline::UniformLight*)sceneData.SceneAllocator->MappedAddress + sceneData.Data->LightOffset, sceneData.Data->LightCount);
        lights[0] =
        {
            .Color          = { 1, .5, .5, 1 },
            .Position       = { 1, 2, 0 },
            .MaxDistance    = 100,
            .Direction      = 0_xyz,
            .ShadowmapIndex = u32(-1),
            .ProjView       = {},
        };
        lights[1] =
        {
            .Color          = { .5, 1, .5, 1 },
            .Position       = { -2, 3, 0 },
            .MaxDistance    = 100,
            .Direction      = 0_xyz,
            .ShadowmapIndex = u32(-1),
            .ProjView       = {},
        };
        lights[2] =
        {
            .Color          = { .5, .5, 1, 1 },
            .Position       = { -2, 3, 0 },
            .MaxDistance    = 100,
            .Direction      = 0_xyz,
            .ShadowmapIndex = u32(-1),
            .ProjView       = {},
        };

        auto lightRot = 0_deg;
        auto lightRotation = 0_deg;
        auto updateLightPos = [&]
        {
            for (auto deltaAng = 360_deg / (f32)lights.size(); auto i : irange(lights.size()))
            {
                auto& l = lights[i];
                auto v = Matrix3::Rotation(0, 0, -lightRot) * (8_up + Matrix3::Rotation(0, f32(i) * deltaAng + lightRotation * 45_deg, 0) * 4_right);
                l.Position = v;
            }
        };

        window.AddScrollCallback([&](Window* win, const Vector2& delta)
        {
            if (!win->IsActive())
                return;
            lightRot += delta.y * 5_deg;
        });

        auto materialCount = 2;
        auto materialOffset = sceneData.SceneAllocator->AllocateIndex32<UniformMaterial>(materialCount);
        auto materials = span((UniformMaterial*)sceneData.SceneAllocator->MappedAddress + materialOffset, materialCount);
        materials[0] =
        {
            .AlbedoMetallicIndex  = u32(-1),
            .NormalRoughnessIndex = u32(-1),
        };
        materials[1] =
        {
            .AlbedoMetallicIndex = 0,
            .NormalRoughnessIndex = 5,
        };

        tp.MaterialIndex = materialOffset;

        auto instId = u32(-1);
        auto instance = (ObjectInstance*)nullptr;

        auto materialIndex = 0;

        auto update = [&, Time = Time()]() mutable
        {
            Window::PollEvents();
            if (window.ShouldClose())
                std::exit(0);
            Time.Update();
            auto& io = ImGui::GetIO();
            if (io.MouseClicked[1])
                lockedCursor = window.CursorPos;
            if (io.MouseDown[1])
            {
                auto delta
                    = (window.GetKey(GLFW_KEY_A) ? 1_left : 0_xyz)
                    + (window.GetKey(GLFW_KEY_D) ? 1_right : 0_xyz)
                    + (window.GetKey(GLFW_KEY_W) ? 1_front : 0_xyz)
                    + (window.GetKey(GLFW_KEY_S) ? 1_back : 0_xyz);

                delta = cameraRotation.RotationMatrix * delta;
                delta.y = 0;

                if (delta.Magnitude > 0)
                    delta.Normalize();

                delta += (window.GetKey(GLFW_KEY_SPACE) ? 1_up : 0_xyz) +
                    (window.GetKey(GLFW_KEY_LEFT_CONTROL) ? 1_down : 0_xyz);

                if (window.GetKey(GLFW_KEY_LEFT_SHIFT))
                    delta *= 3;
                if (window.GetKey(GLFW_KEY_LEFT_ALT))
                    delta *= .1f;

                cameraPosition += delta * (Time.Delta * 3);
                cameraRotation = camRot();

                updateCamera();
                io.MouseDrawCursor = false;
            }
            else io.MouseDrawCursor = true;
            lightRotation += Time.Delta * 45_deg;
            updateLightPos();
        };

        for (auto frameCount : vs::iota(0))
        {
            auto frameIndex = frameCount % frames.size();
            auto frame = frames[frameIndex].get();
            SwapchainTexture* swapTex;
            do //Continually update our app.
            {
                update();
                tie(swapTex) = frame->Begin(&swapchain);
            }
            while (!swapTex);

            auto& tg = targetTextures[frameIndex];

            {
                auto& gpuCamera = *frame->NewObject<AllocatedObject<Pipeline::UniformCamera>>(sceneData.SceneAllocator);
                *gpuCamera = uniformCamera;
                tp.CameraIndex = gpuCamera.Index;

                auto& gpuNDCMatrix = *frame->NewObject<AllocatedObject<Matrix4>>(sceneData.SceneAllocator);
                *gpuNDCMatrix = calcNDC();
                tp.NDCIndex = gpuNDCMatrix.Index;
            }

            {
                auto i = rtp.TextureIndex;
                if (ImGui::IsKeyPressed(ImGuiKey_F1))
                    i = 0;
                else if (ImGui::IsKeyPressed(ImGuiKey_F4))
                    i = (i32)tg.PreviewTextures.size() - 1;
                else if (ImGui::IsKeyPressed(ImGuiKey_F2))
                    i = std::clamp(i - 1, 0, (i32)tg.PreviewTextures.size() - 1);
                else if (ImGui::IsKeyPressed(ImGuiKey_F3))
                    i = std::clamp(i + 1, 0, (i32)tg.PreviewTextures.size() - 1);
                rtp.TextureIndex = i;
            }

            tp.AlbedoMetallic->Target   = tg.Albm.get(); //tp.AlbedoMetallic->ClearValue   = 0_xyzw;
            tp.NormalRoughness->Target  = tg.Nrmr.get(); //tp.NormalRoughness->ClearValue  = .5_xyzw;
            tp.PositionSpecular->Target = tg.Poss.get(); //tp.PositionSpecular->ClearValue = 0_xyzw;
            tp.Overlay->Target          = tg.Overlay.get(); tp.Overlay->ClearValue = 0_xyzw;
            tp.Depth.Target             = tg.Depth.get(); tp.Depth.ClearValue = 1.f;
            tp.Depth.Test = true;

            tp.Bindings.Scenes    = sceneData.SceneBuffer;
            tp.Bindings.Vec3Atts  = sceneData.AttributeBuffer;
            tp.Bindings.STextures = stextures;

            tp.Index->Target = tg.Index.get();

            tg.Index->ClearColorInt(Vector4U32{ u32(-1) }, frame);
            frame->WaitForCommands();

            tp.Begin(frame);
            for (auto [i, o] : loadedScene.Objects | uindexed32)
            {
                auto& m = loadedScene.MeshDatas[o.DataIndex];
                if (m == nullptr)
                    continue;
                tp.Topology = m->CornerPerFace == 3 ? FaceTopology::Tri : FaceTopology::Quad;
                tp.MeshIndex = m->MeshIndex;
                auto& transform = *frame->NewObject<AllocatedObject<Matrix4>>(sceneData.SceneAllocator, 2);
                transform[0] = Matrix4::Transformation(o.Location, o.RotationQuat, o.Scale);
                transform[1] = transform[0].Inverse.Transposed;
                tp.TransformIndex = transform.Index;
                tp.InstanceIndex = i;
                //if (!m->MaterialRanges.empty() && materialIndex < (int)m->MaterialRanges.size())
                //{
                //    auto& [offset, count] = m->MaterialRanges[materialIndex];
                //    tp.Draw({ .VertexCount = count * m->CornerPerFace, .VertexOffset = offset * m->CornerPerFace });
                //}
                //else
                    tp.Draw({ .VertexCount = m->CornerCount, .VertexOffset = 0 });
            }
            tp.End();
            frame->WaitForCommands();

            if (gtaoEnabled)
            {
                auto [viewportWidth, viewportHeight] = window.Size;
                {
                    auto& c = uniformCamera;
                    auto proj = c.Projection;

                    consts.ViewportSize = window.Size;
                    consts.ViewportPixelSize = 1 / (Vector2)window.Size;

                    auto depthLinearizeMul = c.Far * c.Near / (c.Far - c.Near);
                    auto depthLinearizeAdd = c.Far / (c.Far - c.Near);

                    // correct the handedness issue. need to make sure this below is correct, but I think it is.
                    if (depthLinearizeMul * depthLinearizeAdd < 0)
                        depthLinearizeAdd = -depthLinearizeAdd;
                    consts.DepthUnpackConsts = { depthLinearizeMul, depthLinearizeAdd };

                    consts.CameraTanHalfFOV = { 1 / proj[0, 0], 1 / proj[1, 1] };

                    consts.NDCToViewMul = { consts.CameraTanHalfFOV.x, consts.CameraTanHalfFOV.y };
                    consts.NDCToViewAdd = 0_xy;

                    consts.NDCToViewMul_x_PixelSize = { consts.NDCToViewMul.x * consts.ViewportPixelSize.x, consts.NDCToViewMul.y * consts.ViewportPixelSize.y };

                    consts.ViewMatrix = uniformCamera.View;
                }

                auto pf = &gtaoPrefilter;
                pf->PushConstantValue = consts;
                pf->Params.SamplerPointClamp     = &screenSampler;
                pf->Params.g_srcRawDepth         = tg.Depth.get();
                pf->Params.g_outWorkingDepthMIP0 = tg.GtaoDepth->Mipchain[0];
                pf->Params.g_outWorkingDepthMIP1 = tg.GtaoDepth->Mipchain[1];
                pf->Params.g_outWorkingDepthMIP2 = tg.GtaoDepth->Mipchain[2];
                pf->Params.g_outWorkingDepthMIP3 = tg.GtaoDepth->Mipchain[3];
                pf->Params.g_outWorkingDepthMIP4 = tg.GtaoDepth->Mipchain[4];
                pf->Compute({ viewportWidth, viewportHeight, 0 }, frame);
                frame->WaitForCommands();
                
                auto ao = &gtao;
                ao->PushConstantValue = consts;
                ao->Params.SamplerPointClamp  = &screenSampler;
                ao->Params.g_srcNormalmap     = tg.Nrmr.get();
                ao->Params.g_srcWorkingDepth  = tg.GtaoDepth.get();
                ao->Params.g_outWorkingAOTerm = tg.Gtao.get();
                ao->Params.g_outWorkingEdges  = tg.Edge.get();
                ao->Compute({ viewportWidth, viewportHeight, 0 }, frame);
                frame->WaitForCommands();

                if (gtaoSmooth)
                {
                    auto d1 = &gtaoDenoise;
                    d1->PushConstantValue = consts;
                    d1->Params.SamplerPointClamp  = &screenSampler;
                    d1->Params.g_srcWorkingAOTerm = tg.Gtao.get();
                    d1->Params.g_srcWorkingEdges  = tg.Edge.get();
                    d1->Params.g_outFinalAOTerm   = tg.AoInter.get();
                    d1->Compute({ viewportWidth, viewportHeight, 0 }, frame);
                    frame->WaitForCommands();
                    auto d2 = &gtaoDenoise2;
                    d2->PushConstantValue = consts;
                    d2->Params.SamplerPointClamp  = &screenSampler;
                    d2->Params.g_srcWorkingAOTerm = tg.AoInter.get();
                    d2->Params.g_srcWorkingEdges  = tg.Edge.get();
                    d2->Params.g_outFinalAOTerm   = tg.Gtao.get();
                    d2->Compute({ viewportWidth, viewportHeight, 0 }, frame);
                    frame->WaitForCommands();
                }
            }

            {
                auto texs = vector<pair<ITexture*, Sampler*>>{
                    {  tg.Poss.get(),  &screenSampler },
                    {  tg.Albm.get(),  &screenSampler },
                    {  tg.Nrmr.get(),  &screenSampler },
                    {  tg.Gtao.get(),  &screenSampler },
                    { tg.Depth.get(),  &screenSampler },

                    //Test
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },
                    { tg.Poss.get(),  &screenSampler },

                };

                pbr.SceneIndex  = sceneData.Index;
                pbr.CameraIndex = tp.CameraIndex;
                pbr.PositionSpecularIndex = 0;
                pbr.AlbedoMetallicIndex   = 1;
                pbr.NormalRoughnessIndex  = 2;
                pbr.AmbientOcclusionIndex = gtaoEnabled ? 3 : u32(-1);
                pbr.DepthIndex            = 4;

                pbr.Color->Target = tg.Color.get();
                pbr.Color->ClearValue = 0_xyzw;

                pbr.Depth.Target = tg.Depth.get();
                pbr.Depth.Write = false;
                pbr.Depth.CompareOperation = CompareOperation::NotEqual;

                pbr.Bindings.Scenes = sceneData.SceneBuffer;
                pbr.Bindings.Textures = texs;

                pbr.Begin(frame);
                    pbr.DrawTriangle();
                pbr.End();
                frame->WaitForCommands();
            }

            rtp.Depth.Target = tg.Depth.get();
            rtp.Depth.Write = false;
            rtp.Depth.CompareOperation = CompareOperation::NotEqual;

            rtp.Output->Target = swapTex;
            rtp.Bindings.Sampler = &screenSampler;
            rtp.Bindings.Textures = tg.PreviewTextures;
            rtp.UseTonemap   = rtp.TextureIndex == 0;
            rtp.CorrectGamma = rtp.TextureIndex == 0;
            rtp.IsDepth      = rtp.TextureIndex == 5;
            rtp.UseFXAA      = rtp.TextureIndex == 0;

            rtp.RenderAlpha = ImGui::IsKeyDown(ImGuiKey_F);

            rtp.Begin(frame);
                rtp.DrawTriangle();
            rtp.End();

            if (instance)
            {
                auto& o = *instance;
                auto& m = loadedScene.MeshDatas[o.DataIndex];
                assert(m != nullptr);

                auto& transform = *frame->NewObject<AllocatedObject<Matrix4>>(sceneData.SceneAllocator, 2);
                transform[0] = Matrix4::Transformation(o.Location, o.RotationQuat, o.Scale);
                transform[1] = transform[0].Inverse.Transposed;

                tp.Depth.Test = false;
                tp.Begin(frame);
                {
                    tp.Topology = m->CornerPerFace == 3 ? FaceTopology::Tri : FaceTopology::Quad;
                    tp.MeshIndex = m->MeshIndex;
                    tp.TransformIndex = transform.Index;
                    tp.InstanceIndex = instId;
                    tp.Draw({ .VertexCount = m->CornerCount, .VertexOffset = 0 });
                }
                tp.End();
                frame->WaitForCommands();

                op.Culling = FaceCulling::Front;

                op.Overlay->Target = swapTex;
                op.Bindings.Scenes   = sceneData.SceneBuffer;
                op.Bindings.Vec3Atts = sceneData.AttributeBuffer;

                op.Bindings.IndexTexture = tg.Index.get();

                op.CameraIndex   = tp.CameraIndex;
                op.ColorOverride = tp.ColorOverride;
                op.InstanceIndex = instId;

                op.Begin(frame);
                for (auto nm : { .0025f, -.0025f })
                {
                    op.Topology = m->CornerPerFace == 3 ? FaceTopology::Tri : FaceTopology::Quad;
                    op.MeshIndex = m->MeshIndex;
                    op.TransformIndex = transform.Index;
                    op.NormalMultiplier = nm;
                    op.Draw({ .VertexCount = m->CornerCount, .VertexOffset = 0 });
                }
                op.End();
                frame->WaitForCommands();
            }

            imGui.OutputColor->Target = swapTex;
            imGui.Begin(frame);
            {
                using namespace ImGui;
                using namespace ImGuizmo;
                BeginFrame();

                SetDrawlist(GetBackgroundDrawList());
                auto [width, height] = (Vector2)window.Size;
                SetRect(0, 0, width, height);

                auto view = uniformCamera.View * Matrix4::Scale(1, -1, 1);
                auto proj = uniformCamera.Projection;

                //DrawGrid(view, proj, Matrix4::Identity, 100);

                auto transformOf = [](Matrix4 m)
                {
                    auto loc = m[3].xyz;
                    auto scale = Vector3(m[0].Magnitude, m[1].Magnitude, m[2].Magnitude);
                    for (auto& v : m) v.Normalize();
                    auto rot = Quaternion(m);
                    return tuple(loc, rot, scale);
                };

                auto manipulating = false;
                if (auto o = instance)
                {
                    auto mat = Matrix4::Transformation(o->Location, o->RotationQuat, o->Scale);
                    auto delta = Matrix4::Identity;
                    ImGuizmo::PushID(&o->Location);
                    manipulating = Manipulate(view.raw, proj.raw, UNIVERSAL, LOCAL, mat.raw, delta.raw);
                    ImGuizmo::PopID();
                    if (manipulating)
                    {
                        auto [loc, rot, scale] = transformOf(mat);
                        o->Location = loc;
                        o->Scale    = scale;
                        tie(loc, rot, scale) = transformOf(delta);
                        o->RotationQuat = rot * o->RotationQuat;
                        o->Rotation     = o->RotationQuat.EulerAngle;
                    }
                }

                if (auto& io = GetIO(); !manipulating && io.MouseClicked[0] && !io.WantCaptureMouse)
                {
                    auto readPixel = []<class T>(Texture* img, const Vector2U32& loc, T* outValue)
                    {
                        auto buf = MemoryBuffer(img->Device, sizeof(T), vk::BufferUsageFlagBits::eTransferDst, { .DeviceLocal = false, .HostVisible = true });
                        img->Device->ExecuteSingleTimeCommands([&](Frame* fr)
                        {
                            auto [x, y] = (Vector2I32)loc;
                            auto cmd = fr->CommandBuffer;
                            vk::BufferImageCopy region
                            {
                                0,
                                0,
                                0,
                                img->SubresourceLayers,
                                { x, y, 0 },
                                { 1, 1, 1 }
                            };
                            cmd.copyImageToBuffer(img->Instance, vk::ImageLayout::eGeneral, buf.Instance, 1, &region);
                        });
                        *outValue = *(T*)buf.MapMemory();
                        buf.UnmapMemory();
                    };
                    u16 id;
                    readPixel(tg.Index.get(), (Vector2U32)window.CursorPos, &id);
                    instId = id != instId ? id : u32(-1);
                    instance = instId < loadedScene.Objects.size() ? loadedScene.Objects.data() + instId : nullptr;
                }
            }
            {
                using namespace ImGui;
                Begin("Test");
                {
                    if (instance)
                        TextF("Id: {}", instId);
                    else TextF("Id: null");
                    TextF("Hovered Item: {}", instance ? instance->Name : "None");

                    TextF("{}", adapter->Name);
                    TextF("FPS: {:.02f}", GetIO().Framerate);
                    //if (DragInt("FPS Cap", &fps, 1, 10, std::numeric_limits<int>::max()))
                    //    frameTime = nanoseconds(1s) * 1 / std::max(fps, 10.0);

                    SliderInt("Material Index", &materialIndex, 0, 8);

                    if (Checkbox("GTAO", &gtaoEnabled) && !gtaoEnabled)
                        tg.Gtao->ClearColor(0_xyzw, frame);
                    SameLine();
                    Checkbox("Smooth", &gtaoSmooth);

                    DragFloat("EffectRadius",             &consts.EffectRadius,             0.1f);
                    DragFloat("EffectFalloffRange",       &consts.EffectFalloffRange,       0.1f);
                    DragFloat("RadiusMultiplier",         &consts.RadiusMultiplier,         0.1f);
                    DragFloat("FinalValuePower",          &consts.FinalValuePower,          0.1f);
                    DragFloat("DenoiseBlurBeta",          &consts.DenoiseBlurBeta,          0.1f);
                    DragFloat("SampleDistributionPower",  &consts.SampleDistributionPower,  0.1f);
                    DragFloat("ThinOccluderCompensation", &consts.ThinOccluderCompensation, 0.1f);
                    DragFloat("DepthMIPSamplingOffset",   &consts.DepthMIPSamplingOffset,   0.1f);

                    if (DragFloat3("Camera Position", cameraPosition.raw, .1f))
                        updateCamera();
                    if (DragFloat2("Camera Rotation", cameraAngle.raw, .1f))
                        updateCamera();
                    if (ColorEdit4("Wireframe Color", colorOverride.raw))
                        tp.ColorOverride = colorOverride;
                    if (DragFloat("Wireframe Width", &wireFrameWidth, .1f, 0, 5))
                        tp.WireFrameWidth = wireFrameWidth;

                    ColorEdit3("Ambient Light", sceneData.Data->AmbientLight.raw);
                    Spacing();

                    static bool illumination = true;
                    static bool contactShadows = true;
                    static float bias = 0;

                    Checkbox("Illumination", &illumination);
                    Checkbox("Contact Shadows", &contactShadows);
                    DragFloat("Bias", &bias, .001f, 0, .5);

                    pbr.Illumination = illumination;
                    pbr.ContactShadows = contactShadows;
                    pbr.SSSBias = bias;

                    Spacing();

                    for (auto [i, l] : lights | indexed)
                    {
                        TextF("Light[{}]", i);
                        PushID(&l.Color.raw);
                            ColorEdit3("Color", l.Color.raw);
                        PopID();
                        PushID(&l.Color.w);
                            DragFloat("Color Multiplier", &l.Color.w, 0.1f, 1, 20);
                        PopID();
                        PushID(&l.Position.raw);
                            DragFloat3("Position", l.Position.raw, 0.1f);
                        PopID();
                        Spacing();
                    }

                    static float normalStrength = (tp.NormalStrength = 1.0f, tp.NormalStrength);
                    static float metallic       = (tp.Metallic       = 0.0f, tp.Metallic);
                    static float roughness      = (tp.Roughness      = 0.5f, tp.Roughness);
                    if (DragFloat("Normal Strength", &normalStrength, .1f, 0, 5)) tp.NormalStrength = normalStrength;
                    if (DragFloat("metallic",        &metallic,       .1f, 0, 1)) tp.Metallic = metallic;
                    if (DragFloat("roughness",       &roughness,      .1f, 0, 1)) tp.Roughness = roughness;
                }
                End();
                Begin("Morphs");
                {
                    static int comboIndex = 0;
                    auto printObject = [&](ObjectInstance& o)
                    {
                        PushID(&o.DataIndex);
                        if (Button("'{}'"_f(loadedScene.MeshDatas[o.DataIndex]->Name)))
                            comboIndex = o.DataIndex;
                        PopID();
                        PushID(o.Location.raw);
                            DragFloat3("Location", o.Location.raw, 0.1f);
                        PopID();
                        PushID(o.Rotation.raw);
                        if (auto rot = o.Rotation / 1_deg; DragFloat3("Rotation", rot.raw, 0.1f))
                        {
                            o.Rotation = rot * 1_deg;
                            o.RotationQuat = Quaternion::EulerAngles(o.Rotation);
                        }
                        PopID();
                        PushID(o.Scale.raw);
                            DragFloat3("Scale", o.Scale.raw, 0.1f);
                        PopID();
                    };

                    [&](this auto&& self, const unique_ptr<Collection>& col) -> void
                    {
                        if (TreeNode("{}"_f(col->Name).data()))
                        {
                            rn::for_each(col->Children, self);
                            for (auto id : col->ObjectIds) if (auto& o = loadedScene.Objects[id]; TreeNode("{}"_f(o.Name).data()))
                            {
                                printObject(o);
                                TreePop();
                            }
                            TreePop();
                        }
                    }(loadedScene.Collection);

                    if (instance)
                    {
                        Separator();
                        printObject(*instance);
                    }

                    Separator();

                    Combo("Meshes", &comboIndex, (int)loadedScene.MeshDatas.size(), [&](int idx) { return loadedScene.MeshDatas[idx]->Name.data(); });

                    static char chars[0xFF]{};

                    auto mesh = loadedScene.MeshDatas[comboIndex].get();
                    InputText("Filter", chars, std::size(chars));
                    auto updateShapes = false;
                    for (auto& m : mesh->Position->Morphs.Values | vs::drop(1))
                    {
                        if (std::strlen(chars) != 0 && rn::find_icase(m.Name, string_view(+chars)) == m.Name.end())
                            continue;
                        updateShapes |= DragFloat(m.Name.data(), &m.Value, .1f, m.Min, m.Max);
                    }
                    static auto first = true;
                    if ((updateShapes || first) && normalTask.wait_for(0s) == std::future_status::ready)
                    {
                        first = false;
                        mesh->CalcMorphs(frame);
                        frame->WaitForCommands();
                        mesh->CalcFaceNormals(frame);
                        mesh->CalcUvTangents(frame);
                        frame->WaitForCommands();
                        mesh->CalcPointNormals(frame);
                    }
                }
                End();
            }
            imGui.End();

            frame->End();

            threadPool.SubmitVoid([frame, q = device->AcquireQueue(0)] { q->Submit(frame); });
            //device->AcquireQueue(0)->Submit(frame);
        }
    }
    catch (const std::exception& e)
    {
        println("{}", e.what());
    }
}