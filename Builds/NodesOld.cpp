#include "Kaey/ImGui.hpp"
#include "Kaey/Renderer/Renderer.hpp"
#include "Kaey/Scene3D.hpp"
#include "Kaey/ThreadPool.hpp"
#include "Kaey/Time.hpp"
#include "Kaey/Window.hpp"

#include "Kaey/ShaderCompiler/ShaderCompiler.cpp"
#include "Kaey/ShaderCompiler/ShaderTree.cpp"

#include "GBufferPipeline.hpp"
#include "RenderTexPipeline.hpp"

namespace Kaey::Renderer
{
    namespace
    {
        const auto MainRe = std::regex(R"(void\s+main\s*\(\s*\))");
        const auto CaseRe = std::regex(R"(#pragma\s+KR_GBUFFER_CASES\s*\n)");
    }

    class CGBufferPipeline final : public GBufferPipeline
    {
        GlslCompiler compiler;

        vector<u8> vertexSpirv;
        vector<u8> fragmentSpirv;

        string vertexSrc;
        string fragmentSrc;

    public:
        explicit CGBufferPipeline(RenderDevice* device);

        KR_NO_COPY_MOVE(CGBufferPipeline);

        KR_GETTER(cspan<u8>, VertexSpirv) override { return vertexSpirv; }
        KR_GETTER(cspan<u8>, FragmentSpirv) override { return fragmentSpirv; }

        KR_PROPERTY_DECL(string, FragmentCode) { return fragmentSrc; }

    };

    CGBufferPipeline::CGBufferPipeline(RenderDevice* device) :
        GBufferPipeline(device),
        compiler(nullptr),
        vertexSpirv(GBufferPipeline::VertexSpirv | to_vector),
        fragmentSpirv(GBufferPipeline::FragmentSpirv | to_vector),
        vertexSrc(VertexSrc),
        fragmentSrc(FragmentSrc)
    {

    }

    KR_SETTER_DEF(CGBufferPipeline, FragmentCode)
    {
        if (fragmentSrc == value)
            return;
        vector<pair<ShaderType, string>> sources;
        sources.emplace_back(ShaderType::Vertex, vertexSrc);
        sources.emplace_back(ShaderType::Fragment, value);
        auto shaderOutputs = compiler.Compile(sources);
        vertexSpirv = span((u8*)shaderOutputs[0].second.Spirv.data(), 4 * shaderOutputs[0].second.Spirv.size()) | to_vector;
        fragmentSpirv = span((u8*)shaderOutputs[1].second.Spirv.data(), 4 * shaderOutputs[1].second.Spirv.size()) | to_vector;
        fragmentSrc = move(value);
        Reset();
    }
    
}

using namespace Kaey::Renderer;
using namespace Kaey;

const auto Assets = absolute(fs::path("../../../../../Assets"));

int main(int argc, char* argv[])
{
    try
    {
        auto renderEngine = RenderEngine();
        auto threadPool = ThreadPool();

        auto devices = renderEngine.PhysicalDevices;

        if (devices.empty())
        {
            println("No avaliable render device found!");
            return -1;
        }

        auto props = devices | vs::transform([](auto dev) { return dev.getProperties(); }) | to_vector;
        auto deviceNames = props | vs::transform([](auto& prop) -> string { return prop.deviceName.data(); }) | to_vector;
        println("Render devices:\n{}\n", join(deviceNames, "\n"));

        auto& physicalDevice = devices[0];
        println("Using '{}' as render device.\n", deviceNames[0]);

        for (auto&& prop : physicalDevice.getQueueFamilyProperties())
            println("Queue count:\t{:#2},\tflags:\t{}", prop.queueCount, prop.queueFlags);

        auto device = renderEngine.RenderDevices[0];

        const char* matNames[]
        {
            "Ground 12",      //MaterialId(1)
            "Metal Iron 2",   //MaterialId(2)
            "Carbon Fiber 9", //MaterialId(3)
            "Ground 17",      //MaterialId(4)
        };

        auto cgbPipe = CGBufferPipeline(device);

        auto scene = make_unique<Scene3D>(device, &cgbPipe);

        auto loadMaterial = vs::transform([&](auto name)
        {
            auto [albmTex, albmTask] = Texture::LoadSharedAsync(&threadPool, device, Assets / "Textures/{}/albm.png"_f(name), { .Format = vk::Format::eR8G8B8A8Srgb,  .MaxMipLevel = 0, .ClearColor = 1_xyz + 0_w });
            auto [nrmsrTex, nrmsrTask] = Texture::LoadSharedAsync(&threadPool, device, Assets / "Textures/{}/nrmsr.png"_f(name), { .Format = vk::Format::eR8G8B8A8Unorm, .MaxMipLevel = 0, .ClearColor = .5_xyzw });
            auto [paTex, paTask] = Texture::LoadSharedAsync(&threadPool, device, Assets / "Textures/{}/pa.png"_f(name), { .Format = vk::Format::eR8G8B8A8Unorm, .MaxMipLevel = 0, .ClearColor = 1_xyzw });
            auto matId = scene->CreateMaterial();
            if (albmTex)
                scene->SetMaterialAlbedoMetallicTexture(matId, scene->AddTexture(move(albmTex)));
            if (nrmsrTex)
                scene->SetMaterialNormalSpecularRoughness(matId, scene->AddTexture(move(nrmsrTex)));
            if (paTex)
                scene->SetMaterialParallaxAlpha(matId, scene->AddTexture(move(paTex)));
            return matId;
        });

        auto mats = matNames | loadMaterial | to_vector;

        scene->EnvironmentMultiplier = .05_xyz + 1_w;

        auto [exrTex, exrTask] = Texture::LoadExrUniqueAsync(&threadPool, device, Assets / "Textures/HDRIs/courtyard_4k.exr");
        auto exrId = scene->AddTexture(exrTex.get());

        scene->LoadGltf(Assets / "monkey.glb");

        auto camId = scene->CreateCamera();
        auto cam = &scene->Cameras[camId];
        auto cameraAngle = Vector2(180_deg, 0_deg);
        auto camRot = [&] { return Quaternion::AngleAxis(cameraAngle.y, Vector3::Right) * Quaternion::AngleAxis(cameraAngle.x, Vector3::Up); };

        scene->SetCameraPosition(camId, { 0, 1.7f, 5 });
        scene->SetCameraRotation(camId, camRot());
        scene->SetCameraScreenSize(camId, Vector2{ 1920, 1080 } *1.0f);
        scene->SetCameraFov(camId, 90_deg);

        auto lightRot = 0_deg;
        auto lightRotation = 0_deg;
        auto updateLightPos = [&]
        {
            auto lights = scene->Lights;
            for (auto deltaAng = 360_deg / (f32)lights.size(); auto i : irange(lights.size()))
            {
                auto id = LightId(i);
                auto v = Matrix3::Rotation(0, 0, -lightRot) * (Vector3::Up * 8 + Matrix3::Rotation(0, f32(i) * deltaAng + lightRotation * 45_deg, 0) * Vector3::Right * 4);
                scene->SetLightPosition(id, v);
                scene->SetLightRotation(id, Quaternion::EulerAngles(0, 0, lightRot));
            }
        };

        scene->SetLightColor(scene->CreateLight(), { 1.0, 0.5, 0.5, 10 });
        scene->SetLightColor(scene->CreateLight(), { 0.5, 1.0, 0.5, 10 });
        scene->SetLightColor(scene->CreateLight(), { 0.5, 0.5, 1.0, 10 });

        for (auto [i, l] : scene->Lights | indexed32)
        {
            auto id = LightId(i);
            scene->SetLightMaxRadius(id, 90_deg);
            scene->SetLightColor(id, { l.Color.xyz, 10 });
        }

        //auto lightId = scene->CreatePointLight();
        //{
        //    scene->SetPointLightColor(lightId, { 1_xyz, 3 });
        //    scene->SetPointLightPosition(lightId, { 0, 2, 0 });
        //}

        auto window = Window
        {
            device,
            "Kaey Renderer",
            {
                { GLFW_CLIENT_API, GLFW_NO_API },
                { GLFW_RESIZABLE, GLFW_FALSE },
                { GLFW_DECORATED, GLFW_FALSE },
                { GLFW_AUTO_ICONIFY, GLFW_TRUE },
            }
        };

        auto instanceImGui = ImGuiInstance(&window);

        auto swapchain = make_unique<Swapchain>(&window, device);

        auto rtp = make_unique<RenderTexPipeline>(device);
        auto sampler = device->Instance.createSamplerUnique({
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            {},
            VK_FALSE, //Anisotropy
            16, //Max Anisotropy
            VK_FALSE,
            vk::CompareOp::eNever,
            0, //Min Lod
            0, //Max Lod
            vk::BorderColor::eFloatOpaqueBlack,
        });
        rtp->WriteSampler(sampler);

        auto frames =
            irange(swapchain->MaxFrames)
            | vs::transform([=](auto) { return make_unique<Frame>(device); })
            | to_vector
            ;

        auto screenCenter = window.Size / 2;
        window.SetCursorPos(screenCenter);

        bool demoWindow = false;

        window.AddCursorPosCallback([&](Window* win, const Vector2& currentPos)
        {
            if (!win->IsActive() || demoWindow)
                return;
            auto delta = currentPos - screenCenter;
            cameraAngle -= Matrix2::Scale2D(25_deg, 15_deg) * delta * .01f;
            cameraAngle.y = std::clamp(cameraAngle.y, -89_deg, 89_deg);
            win->SetCursorPos(screenCenter);
        });

        window.AddScrollCallback([&](Window* win, const Vector2& delta)
        {
            if (!win->IsActive() || demoWindow)
                return;
            lightRot += delta.y * 5_deg;
        });

        window.AddKeyCallback([&](Window* win, int key, int scancode, int action, int mods)
        {
            switch (key)
            {
            case GLFW_KEY_INSERT:
            {
                if (action == GLFW_PRESS)
                    demoWindow = !demoWindow;
                if (!demoWindow)
                    win->SetCursorPos(screenCenter);
            }break;
            }
        });

        auto boxMat = mats[0];
        auto metalMat = mats[1];
        auto carbonMat = mats[2];
        auto brickMat = mats[3];

        scene->SetMaterialUvMultiplier(boxMat, { 4, 4 });
        //scene->SetMaterialAlbedoMultiplier(boxMat, 1.2_xyz);
        scene->SetMaterialNormalMultiplier(boxMat, 5);

        //scene->SetMaterialNormalMultiplier(brickMat, 5.0f);
        scene->SetMaterialParallaxAlpha(brickMat, TextureId(-1));

        scene->SetMaterialUvMultiplier(metalMat, { 5, 5 });

        //scene->SetMaterialAlphaMultiplier(boxMat, .85f);
        //scene->SetMaterialAlphaMultiplier(metalMat, .9f);
        //scene->SetMaterialAlphaMultiplier(carbonMat, .65f);
        //scene->SetMaterialAlphaMultiplier(brickMat, .65f);

        auto monkey = MeshInstanceId(0);
        auto sqrBall = MeshInstanceId(1);
        auto ball = MeshInstanceId(2);
        auto box = MeshInstanceId(3);

        scene->SetMeshMaterial(box, 0, boxMat);
        //scene->SetMeshMaterial(monkey, 0, metalMat);
        scene->SetMeshMaterial(sqrBall, 0, brickMat);
        scene->SetMeshMaterial(ball, 0, carbonMat);

        updateLightPos();

        scene->Update();

        vector<pair<int, ITexture*>> textureTargets
        {
            { GLFW_KEY_1, scene->Textures[cam->OutputId] },
            { GLFW_KEY_7, scene->Textures[cam->DiffuseId] },
            { GLFW_KEY_8, scene->Textures[cam->SpecularId] },
            { GLFW_KEY_2, scene->Textures[cam->AlbedoMetallicId] },
            { GLFW_KEY_3, scene->Textures[cam->NormalSpecularRoughnessId] },
            { GLFW_KEY_4, scene->Textures[cam->NormalSpecularRoughnessId] }, //Specular
            { GLFW_KEY_5, scene->Textures[cam->AmbientOcclusionId] },
            { GLFW_KEY_6, scene->Textures[cam->DepthId] },
            { GLFW_KEY_9, scene->Textures[cam->TranslucencyId] },
        };

        for (auto [i, mip] : scene->Textures[cam->BloomId]->Mipchain | vs::take(9) | indexed32)
            textureTargets.emplace_back(GLFW_KEY_KP_0 + i, mip);

        u32 screenShotCount = 0;
        rtp->WriteTextures(textureTargets | vs::values | to_vector);

        auto nodeMaterial = NodeMaterial(scene.get());
        nodeMaterial.CreateNode<ShaderOutputNode>();

        while (!window.ShouldClose())
        {
            {
                lightRotation += scene->Time->Delta * 45_deg;
                updateLightPos();
                scene->SetMeshDataShapeDelta(scene->MeshInstances[sqrBall].DataId, 0, PingPong(scene->Time->Elapsed * 5, 5.f) - 2.5f);
            }
            if (!demoWindow)
            {
                Vector3 delta;
                if (window.GetKey(GLFW_KEY_A))
                    delta += Vector3::Left;
                if (window.GetKey(GLFW_KEY_D))
                    delta += Vector3::Right;
                if (window.GetKey(GLFW_KEY_W))
                    delta += Vector3::Forward;
                if (window.GetKey(GLFW_KEY_S))
                    delta += Vector3::Backward;
                delta = cam->Rotation.RotationMatrix * delta;
                delta.y = 0;
                if (delta.SqrMagnitude > 0)
                    delta.Normalize();
                if (window.GetKey(GLFW_KEY_SPACE))
                    delta += Vector3::Up;
                if (window.GetKey(GLFW_KEY_LEFT_CONTROL))
                    delta += Vector3::Down;
                if (window.GetKey(GLFW_KEY_LEFT_SHIFT))
                    delta *= 3;
                if (window.GetKey(GLFW_KEY_LEFT_ALT))
                    delta *= .1f;

                {
                    auto lightDelta = 0_xyz;
                    if (window.GetKey(GLFW_KEY_KP_4))
                        lightDelta += Vector3::Right;
                    if (window.GetKey(GLFW_KEY_KP_6))
                        lightDelta += Vector3::Left;
                    if (window.GetKey(GLFW_KEY_KP_8))
                        lightDelta += Vector3::Up;
                    if (window.GetKey(GLFW_KEY_KP_5))
                        lightDelta += Vector3::Down;
                    if (window.GetKey(GLFW_KEY_KP_7))
                        lightDelta += Vector3::Forward;
                    if (window.GetKey(GLFW_KEY_KP_9))
                        lightDelta += Vector3::Backward;
                    //scene->SetPointLightPosition(lightId, scene->PointLights[lightId].Position + 3 * lightDelta * scene->Time->Delta);
                }

                for (auto [i, key, tex] : textureTargets | indexed32) if (window.GetKey(key))
                {
                    rtp->TextureIndex = i;
                    break;
                }

                scene->SetCameraPosition(camId, cam->Position + delta * (scene->Time->Delta * 3));
                scene->SetCameraRotation(camId, Quaternion::SLerp(cam->Rotation, camRot(), scene->Time->Delta * 20));
            }

            if (exrTask.valid() && exrTask.wait_for(0s) == std::future_status::ready)
            {
                scene->EnvironmentTexture = exrId;
                exrTask = {};
            }

            auto frame = frames[swapchain->CurrentIndex].get();
            frame->Begin();

            scene->Update(frame);

            scene->Render(frame);

            rtp->Output = swapchain->CurrentTexture;
            rtp->IsDepth = rtp->TextureIndex == 5 + 2;
            rtp->IsNormal = rtp->TextureIndex == 2 + 2;
            rtp->IsSpecular = rtp->TextureIndex == 3 + 2;
            rtp->IsAO = rtp->TextureIndex == 4 + 2;
            rtp->CorrectGamma = rtp->TextureIndex <= 2;
            rtp->UseTonemap = rtp->TextureIndex <= 2;
            rtp->RenderAlpha = window.GetKey(GLFW_KEY_Q);
            rtp->Near = cam->Near;
            rtp->Far = cam->Far;
            rtp->Begin(frame);
                rtp->DrawTriangle();
            rtp->End();

            instanceImGui.OutputColor = swapchain->CurrentTexture;
            instanceImGui.Begin(frame);
            if (ImGui::GetIO().MouseDrawCursor = demoWindow)
            {
                using namespace ImGui;
                auto& io = GetIO();

                if (Begin("Nodes"))
                {
	                Text("FPS: %.2f (%.2gms)", io.Framerate, 1000 / io.Framerate);

	                Separator();

	                nodeMaterial.OnGui();
                }
                End();

                if (Begin("Code"))
                {
                    if (auto root = nodeMaterial.SelectedOutput)
                    {
                        stringstream ss;

                        ss << "bool Material1()\n{\n";

                        auto visitor = ShaderVisitor(ss, scene->NodeValueOffset);
                        visitor.Visit(root);

                        ss << "\n}\n";

                        auto text = ss.str();

                        ss << "\nvoid main()";

                        auto code = regex_replace(string(GBufferPipeline::FragmentSrc), MainRe, ss.str());

                        ss.clear();
                        ss.str({});

                        ss << "    case "<< scene->MaterialOffset << ": if (!Material1()) discard; break;\n";

                        code = regex_replace(code, CaseRe, ss.str());

                        try
                        {
                            cgbPipe.FragmentCode = code;
                        }
                        catch (std::exception& e)
                        {
                            text += "\n\n";
                            text += e.what();
                        }
                        TextUnformatted(text.data());

                        std::memcpy(scene->NodeValues + scene->NodeValueOffset, nodeMaterial.NodeValues.data(), nodeMaterial.NodeValues.size() * sizeof(GBufferPipeline::NodeValue));

                        //writer.QueueWrite(cgbPipe.NodeBuffer, 0, nodeMaterial.NodeValues);
                    }
                }
                End();

                if (Begin("Output"))
                {
                    Image(scene->Textures[cam->OutputId], 512_xy / 1.5f);
                    Image(scene->Textures[cam->DiffuseId], 512_xy / 1.5f);
                    Image(scene->Textures[cam->SpecularId], 512_xy / 1.5f);
                }
                End();

            }
            if (false)
            {
                using namespace ImGui;
                auto& io = GetIO();
                io.MouseDrawCursor = demoWindow;
                if (demoWindow && Begin("Settings", &demoWindow))
                {
                    for (auto id : irange(scene->Materials.size()))
                    {
                        LabelText("Material", "%llu", id);
                        Material(scene.get(), MaterialId(id));
                        Separator();
                    }
                    End();
                }
                else window.SetCursorPos(screenCenter);
                if (demoWindow && Begin("Camera", &demoWindow))
                {
                    Text("FPS: %.2f (%.2gms)", io.Framerate, 1000 / io.Framerate);
                    Separator();

                    Camera(scene.get(), camId);

                    Image(scene->Textures[cam->OutputId], cam->ScreenSize / 5);
                    SameLine();
                    Image(scene->Textures[cam->NormalSpecularRoughnessId], cam->ScreenSize / 5);

                    Image(scene->Textures[cam->AmbientOcclusionId], cam->ScreenSize / 5);
                    SameLine();
                    Image(scene->Textures[cam->AlbedoMetallicId], cam->ScreenSize / 5);

                    End();
                }
            }
            instanceImGui.End();

            unique_ptr<Texture> screenShot;
            if (window.GetKey(GLFW_KEY_F11))
            {
                screenShot = make_unique<Texture>(device, TextureArgs{ scene->Textures[cam->OutputId]->Size, vk::Format::eR8G8B8A8Unorm });
                rtp->Output = screenShot.get();
                rtp->Begin(frame);
                    rtp->DrawTriangle();
                rtp->End();
            }

            frame->End();

            swapchain->Present();

            if (screenShot)
                screenShot->Save(Assets / "ScreenShoot{}.png"_f(++screenShotCount));

            Window::PollEvents();
        }
        if (threadPool.WorkingThreadCount > 0)
            exit(0); //Exit immediatly if background tasks are still executing.
    }
    catch (const std::exception& e)
    {
        println("{}", e.what());
    }
}
