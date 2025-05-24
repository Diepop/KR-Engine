#include "Kaey/Renderer/ImGui.hpp"
#include "Kaey/Renderer/Renderer.hpp"
#include "Kaey/Renderer/Scene3D.hpp"
#include "Kaey/Renderer/ThreadPool.hpp"
#include "Kaey/Renderer/Time.hpp"
#include "Kaey/Renderer/Window.hpp"

#include <RenderTexPipeline.hpp>

// TODO
// [V] Normal Map
// [V] Spotlights
// [V] Directional Lights
// [V] Shadowmapping
// [V] Screen Space Ambient Occlusion (SSAO)
// [V] Texture Mipmaps
// [V] Cubemap Textures (Implement Separate Class)
// [V] Environtment Map
// [V] Pointlight Shadowmapping
// [V] Shape Keys
// [V] Bump Map
// [V] Bloom
// [V] FXAA
// [V] Allocator for GPU Buffers (Buddy)
// [V] Compute (Implement Single Tool)
// [V] Separated Vertex Buffer
// [V] ImGui
// [V] Exr and HDRI maps
// [V] Diffuse Irradiance and Specular Reflections from HDRIs
// [V] GTAO (Like Blender EEVEE)
// [~] Subsurface Scattering (SSSS)
// [~] Transparency (Support Textures)
// [~] Parallax Mapping and Occlusion
// [X] Static Pipeline Variance
// [X] Mesh Modifiers
// [X] Armature
// [X] Noise Textures
// [X] Volumetric Rendering (GodRays)
// [X] Screen Space Reflections (SSR)
// [X] Dynamic Cubemap Reflections
// [X] Refraction
// [X] Diffraction
// [X] Static Render Pipelines (For Android)
// [X] UDIM Textures
// [X] Transform Parenting
// [X] Use mapped memory instead of writer for scene data.

using namespace Kaey::Renderer;
using namespace Kaey;

extern const auto Assets = fs::path(ASSETS_PATH);
extern const auto Shaders = fs::path(SHADERS_PATH);

void LoadSceneMaterials(Scene3D* scene, ThreadPool* threadPool)
{
    const char* matNames[]
    {
        "Ground 12",      //MaterialId(1)
        "Metal Iron 2",   //MaterialId(2)
        "Carbon Fiber 9", //MaterialId(3)
        "Ground 17",      //MaterialId(4)
    };
    
    auto loadMaterial = vs::transform([&](auto name)
    {
        auto [albmTex, albmTask]   = Texture::LoadSharedAsync(threadPool, scene->Device, Assets / "Textures/{}/albm.png"_f(name),  { .Format = vk::Format::eR8G8B8A8Srgb,  .MaxMipLevel = 0, .ClearColor = 1_xyz + 0_w });
        auto [nrmsrTex, nrmsrTask] = Texture::LoadSharedAsync(threadPool, scene->Device, Assets / "Textures/{}/nrmsr.png"_f(name), { .Format = vk::Format::eR8G8B8A8Unorm, .MaxMipLevel = 0, .ClearColor = .5_xyzw });
        auto [paTex, paTask]       = Texture::LoadSharedAsync(threadPool, scene->Device, Assets / "Textures/{}/pa.png"_f(name),    { .Format = vk::Format::eR8G8B8A8Unorm, .MaxMipLevel = 0, .ClearColor = 1_xyzw });
        auto matId = scene->CreateMaterial();
        if (albmTex)  scene->SetMaterialAlbedoMetallicTexture(matId, scene->AddTexture(move(albmTex)));
        if (nrmsrTex) scene->SetMaterialNormalSpecularRoughness(matId, scene->AddTexture(move(nrmsrTex)));
        if (paTex)    scene->SetMaterialParallaxAlpha(matId, scene->AddTexture(move(paTex)));
        return matId;
    });

    auto mats = matNames | loadMaterial | to_vector;

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

    auto monkey  = MeshInstanceId(0);
    auto sqrBall = MeshInstanceId(1);
    auto ball    = MeshInstanceId(2);
    auto box     = MeshInstanceId(3);

    scene->SetMeshMaterial(box,     0, boxMat);
    scene->SetMeshMaterial(monkey,  0, metalMat);
    scene->SetMeshMaterial(sqrBall, 0, brickMat);
    scene->SetMeshMaterial(ball,    0, carbonMat);
}

int main(int argc, char* argv[])
{
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

        auto deviceInst = RenderDevice(adapter);
        auto device = &deviceInst;

        current_path(Shaders);
        auto scene = make_unique<Scene3D>(device);

        scene->EnvironmentMultiplier = .25_xyz + 1_w;

        auto [exrTex, exrTask] = Texture::LoadExrUniqueAsync(&threadPool, device, Assets / "Textures/HDRIs/courtyard_4k.exr");
        auto exrId = scene->AddTexture(exrTex.get());

        scene->LoadGltf(Assets / "monkey.glb");
        LoadSceneMaterials(scene.get(), &threadPool);

        auto camId = scene->CreateCamera();
        auto cam = &scene->Cameras[camId];
        auto cameraAngle = Vector2(180_deg, 0_deg);
        auto camRot = [&]{ return Quaternion::AngleAxis(cameraAngle.y, 1_right) * Quaternion::AngleAxis(cameraAngle.x, 1_up); };

        scene->SetCameraPosition(camId, { 0, 1.75f, 5 });
        scene->SetCameraRotation(camId, camRot());
        scene->SetCameraScreenSize(camId, Vector2{ 1920, 1080 } * 1.0f);
        scene->SetCameraFov(camId, 106_deg);
        //scene->SetCameraBloom(camId, false);
        //scene->SetCameraSSAO(camId, false);

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
        //    scene->SetPointLightPosition(lightId, { 0, 2, 2 });
        //
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

        auto swapchain = make_unique<Swapchain>(&window, SwapchainArgs{ .VerticalSync = true });

        auto rtp = make_unique<RenderTexPipeline>(device);
        rtp->WriteSampler(scene->ScreenSampler);

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

        updateLightPos();

        scene->Update();

        vector<pair<int, ITexture*>> textureTargets
        {
            { GLFW_KEY_1, scene->Textures[cam->OutputId] },
            { GLFW_KEY_2, scene->Textures[cam->AlbedoMetallicId] },
            { GLFW_KEY_3, scene->Textures[cam->NormalId] },
            { GLFW_KEY_4, scene->Textures[cam->PositionId] },
            { GLFW_KEY_5, scene->Textures[cam->AmbientOcclusionId] },
            { GLFW_KEY_6, scene->Textures[cam->DepthId] },
            //{ GLFW_KEY_7, scene->Textures[cam->DiffuseId] },
            //{ GLFW_KEY_8, scene->Textures[cam->SpecularId] },
            //{ GLFW_KEY_9, scene->Textures[cam->TranslucencyIorId] },
        };

        for (auto [i, mip] : scene->Textures[cam->BloomId]->Mipchain | vs::take(9) | indexed32)
            textureTargets.emplace_back(GLFW_KEY_KP_0 + i, mip);

        u32 screenShotCount = 0;
        rtp->WriteTextures(textureTargets | vs::values | to_vector);
        while (!window.ShouldClose())
        {
            {
                lightRotation += scene->Time->Delta * 45_deg;
                updateLightPos();
                auto sqrBall = MeshInstanceId(1);
                scene->SetMeshDataShapeDelta(scene->MeshInstances[sqrBall].DataId, 0, PingPong(scene->Time->Elapsed * 5, 5.f) - 2.5f);
            }
            if (!demoWindow)
            {
                Vector3 delta;
                if (window.GetKey(GLFW_KEY_A))
                    delta += 1_left;
                if (window.GetKey(GLFW_KEY_D))
                    delta += 1_right;
                if (window.GetKey(GLFW_KEY_W))
                    delta += 1_front;
                if (window.GetKey(GLFW_KEY_S))
                    delta += 1_back;
                delta = cam->Rotation.RotationMatrix * delta;
                delta.y = 0;
                if (delta.SqrMagnitude > 0)
                    delta.Normalize();
                if (window.GetKey(GLFW_KEY_SPACE))
                    delta += 1_up;
                if (window.GetKey(GLFW_KEY_LEFT_CONTROL))
                    delta += 1_down;
                if (window.GetKey(GLFW_KEY_LEFT_SHIFT))
                    delta *= 3;
                if (window.GetKey(GLFW_KEY_LEFT_ALT))
                    delta *= .1f;

                {
                    auto lightDelta = 0_xyz;
                    if (window.GetKey(GLFW_KEY_KP_4))
                        lightDelta += 1_right;
                    if (window.GetKey(GLFW_KEY_KP_6))
                        lightDelta += 1_left;
                    if (window.GetKey(GLFW_KEY_KP_8))
                        lightDelta += 1_up;
                    if (window.GetKey(GLFW_KEY_KP_5))
                        lightDelta += 1_down;
                    if (window.GetKey(GLFW_KEY_KP_7))
                        lightDelta += 1_front;
                    if (window.GetKey(GLFW_KEY_KP_9))
                        lightDelta += 1_back;
                    //scene->SetPointLightPosition(lightId, scene->PointLights[lightId].Position + 3 * lightDelta * scene->Time->Delta);
                }

                for (auto [i, key, tex] : textureTargets | indexed32) if (window.GetKey(key))
                {
                    rtp->TextureIndex = i;
                    break;
                }

                scene->SetCameraPosition(camId, cam->Position + delta * (scene->Time->Delta * 3));
                scene->SetCameraRotation(camId, slerp(cam->Rotation, camRot(), scene->Time->Delta * 20));
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

            rtp->Output         = swapchain->CurrentTexture;
            rtp->IsDepth        = rtp->TextureIndex == 5;
            //rtp->IsSpecular     = rtp->TextureIndex == 3 + 2;
            rtp->IsAO           = rtp->TextureIndex == 4;
            //rtp->IsPackedNormal = rtp->TextureIndex == 2;
            rtp->CorrectGamma   = rtp->TextureIndex <= 1;
            rtp->UseTonemap     = rtp->TextureIndex <= 1 || rtp->TextureIndex == 3;
            rtp->RenderAlpha    = window.GetKey(GLFW_KEY_Q);
            rtp->Near           = cam->Near;
            rtp->Far            = cam->Far;
            rtp->Begin(frame);
                rtp->DrawTriangle();
            rtp->End();

            instanceImGui.OutputColor = swapchain->CurrentTexture;
            instanceImGui.Begin(frame);
            {
                using namespace ImGui;
                auto& io = GetIO();
                io.MouseDrawCursor = demoWindow;
                if (demoWindow)
                {
                    if (Begin("Settings"))
                    {
                        for (auto id : irange(scene->Materials.size()))
                        {
                            LabelText("Material", "%llu", id);
                            Material(scene.get(), MaterialId(id));
                            Separator();
                        }
                    }
                    End();
                    if (Begin("Camera"))
                    {
                        Text("FPS: %.2f (%.2gms)", io.Framerate, 1000 / io.Framerate);
                        Separator();

                        Camera(scene.get(), camId);

                        Image(scene->Textures[cam->OutputId], cam->ScreenSize / 5);
                        SameLine();
                        //Image(scene->Textures[cam->NormalId], cam->ScreenSize / 5);

                        Image(scene->Textures[cam->AmbientOcclusionId], cam->ScreenSize / 5);
                        SameLine();
                        Image(scene->Textures[cam->AlbedoMetallicId], cam->ScreenSize / 5);

                    }
                    End();
                }
                else window.SetCursorPos(screenCenter);
                
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
