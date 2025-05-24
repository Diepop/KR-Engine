#include "Kaey/Renderer/DynamicRenderPipeline.hpp"
#include "Kaey/Renderer/ImGui.hpp"
#include "Kaey/Renderer/Renderer.hpp"
#include "Kaey/Renderer/ThreadPool.hpp"
#include "Kaey/Renderer/Time.hpp"
#include "Kaey/Renderer/Utility.hpp"
#include "Kaey/Renderer/Window.hpp"

#include <RenderTexPipeline.hpp>

#include <Slang/SSSProfilePipeline.hpp>

using namespace Kaey::Renderer;
using namespace Kaey;

extern const auto Assets  = fs::path(ASSETS_PATH);
extern const auto Shaders = fs::path(SHADERS_PATH);

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

        auto screenSampler = Sampler(device, { .Interpolation = Interpolation::Linear, .Extrapolation = Extrapolation::Clip, });
        auto rtp = RenderTexPipeline(device);

        rtp.TextureIndex = 0;

        auto imGui = ImGuiInstance(&window);

        auto profileColor = 1_xyz;
        auto directLight = false;

        auto update = [&, Time = Time()]() mutable
        {
            Window::PollEvents();
            if (window.ShouldClose())
                std::exit(0);
            Time.Update();
        };

        auto sss = Renderer::Slang::SSSProfilePipeline(device);
        auto sssTex = Texture(device, { .Size = 1024_xyu, .Format = eR8G8B8A8Unorm, .MaxMipLevel = 1, .ClearColor = 0_xyzw });

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

            sss.Profile->Target = &sssTex;
            sss.Profile->Target = swapTex;
            sss.Color = profileColor;
            sss.ViewportSize = (Vector2)window.Size;
            sss.DirectLight = directLight;
            sss.Begin(frame);
                //sss.Scissor = ScissorArea{ .Offset = swapTex->Size / 4, .Size = swapTex->Size / 2 };
                sss.DrawTriangle();
            sss.End();

            imGui.OutputColor->Target = swapTex;
            imGui.Begin(frame);
            {
                using namespace ImGui;
                if (Begin("Hello"))
                {
                    TextF("{}", adapter->Name);
                    TextF("FPS: {:.02f}", GetIO().Framerate);
                    ColorPicker3("Color", profileColor.raw);
                    Checkbox("Direct Light", &directLight);
                }
                End();

                //if (Begin("Profile"))
                //{
                //    //auto [w, h] = sssTex.Size;
                //    Image((ImTextureID)(ITexture*)&sssTex, { 512, 512 });
                //}
                //End();
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