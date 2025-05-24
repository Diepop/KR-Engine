#include "Kaey/ImGui.hpp"
#include "Kaey/Renderer/Renderer.hpp"
#include "Kaey/ThreadPool.hpp"
#include "Kaey/Window.hpp"

#include "Kaey/Shader/GlslShader.cpp"
#include "Kaey/Shader/NodeShader.cpp"
#include "Kaey/Shader/Shader.cpp"

#include <imgui_internal.h>

using namespace Kaey::Renderer::Shader;
using namespace Kaey::Renderer;
using namespace Kaey;

extern const auto Assets = fs::path(ASSETS_PATH);
extern const auto Shaders = fs::path(SHADERS_PATH);

namespace
{
    void ShowNodeStyleEditor(bool* show = nullptr)
    {
        static auto first = []
        {
            auto& s = ed::GetStyle();
            auto& gs = ImGui::GetStyle();
            gs.Colors[ImGuiCol_FrameBg] = ImColor(0xFF545454);

            s.Colors[ed::StyleColor_Bg] = ImColor(0xFF1D1D1D);
            s.Colors[ed::StyleColor_NodeBg] = ImColor(0xFF303030);
            s.Colors[ed::StyleColor_NodeBorder] = ImColor(0xFF101010);
            s.Colors[ed::StyleColor_SelNodeBorder] = ImColor(0xFFFDFDFD);
            s.Colors[ed::StyleColor_NodeSelRectBorder] = ImColor(0xFF666666);
            s.Colors[ed::StyleColor_NodeSelRect] = ImColor(0x33666666);

            s.Colors[ed::StyleColor_HovNodeBorder] = ImColor(0x00FFFFFF);
            s.Colors[ed::StyleColor_HovLinkBorder] = ImColor(0x00FFFFFF);
            s.Colors[ed::StyleColor_PinRect] = ImColor(0x00FFFFFF);

            s.NodeRounding = 4;
            s.NodePadding = { -9, 0, -10, 0 };
            s.LinkStrength = 125;

            return true;
        }();

        if (!ImGui::Begin("Style", show))
        {
            ImGui::End();
            return;
        }

        auto paneWidth = ImGui::GetContentRegionAvail().x;

        auto& editorStyle = ed::GetStyle();
        ImGui::TextUnformatted("Values");
        ImGui::SameLine();
        if (ImGui::Button("Reset to defaults"))
            editorStyle = ed::Style();
        ImGui::Spacing();
        ImGui::DragFloat4("Node Padding", &editorStyle.NodePadding.x, 0.5f, -40.0f, 40.0f);
        ImGui::DragFloat("Node Rounding", &editorStyle.NodeRounding, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Node Border Width", &editorStyle.NodeBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Hovered Node Border Width", &editorStyle.HoveredNodeBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Hovered Node Border Offset", &editorStyle.HoverNodeBorderOffset, 0.1f, -40.0f, 40.0f);
        ImGui::DragFloat("Selected Node Border Width", &editorStyle.SelectedNodeBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Selected Node Border Offset", &editorStyle.SelectedNodeBorderOffset, 0.1f, -40.0f, 40.0f);
        ImGui::DragFloat("Pin Rounding", &editorStyle.PinRounding, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Pin Border Width", &editorStyle.PinBorderWidth, 0.1f, 0.0f, 15.0f);
        ImGui::DragFloat("Link Strength", &editorStyle.LinkStrength, 1.0f, 0.0f, 500.0f);
        //ImVec2  SourceDirection;
        //ImVec2  TargetDirection;
        ImGui::DragFloat("Scroll Duration", &editorStyle.ScrollDuration, 0.001f, 0.0f, 2.0f);
        ImGui::DragFloat("Flow Marker Distance", &editorStyle.FlowMarkerDistance, 1.0f, 1.0f, 200.0f);
        ImGui::DragFloat("Flow Speed", &editorStyle.FlowSpeed, 1.0f, 1.0f, 2000.0f);
        ImGui::DragFloat("Flow Duration", &editorStyle.FlowDuration, 0.001f, 0.0f, 5.0f);
        //ImVec2  PivotAlignment;
        //ImVec2  PivotSize;
        //ImVec2  PivotScale;
        //float   PinCorners;
        //float   PinRadius;
        //float   PinArrowSize;
        //float   PinArrowWidth;
        ImGui::DragFloat("Group Rounding", &editorStyle.GroupRounding, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Group Border Width", &editorStyle.GroupBorderWidth, 0.1f, 0.0f, 15.0f);

        ImGui::Separator();

        static ImGuiColorEditFlags edit_mode = ImGuiColorEditFlags_DisplayRGB;
        ImGui::TextUnformatted("Filter Colors");
        ImGui::SameLine();
        ImGui::RadioButton("RGB", &edit_mode, ImGuiColorEditFlags_DisplayRGB);
        ImGui::SameLine();
        ImGui::RadioButton("HSV", &edit_mode, ImGuiColorEditFlags_DisplayHSV);
        ImGui::SameLine();
        ImGui::RadioButton("HEX", &edit_mode, ImGuiColorEditFlags_DisplayHex);

        static ImGuiTextFilter filter;
        filter.Draw("##filter", paneWidth);

        ImGui::Spacing();

        ImGui::PushItemWidth(-160);
        for (int i = 0; i < ed::StyleColor_Count; ++i)
        {
            auto name = GetStyleColorName((ed::StyleColor)i);
            if (!filter.PassFilter(name))
                continue;

            ImGui::ColorEdit4(name, &editorStyle.Colors[i].x, edit_mode);
        }
        ImGui::PopItemWidth();

        ImGui::End();
    }

}

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

        auto swapchain = make_unique<Swapchain>(&window, device);

        auto frames =
            irange(swapchain->MaxFrames)
            | vs::transform([=](auto) { return make_unique<Frame>(device); })
            | to_vector
            ;

        auto screenCenter = window.Size / 2;
        window.SetCursorPos(screenCenter);

        auto instanceImGui = ImGuiInstance(&window);
        auto imguiTextures =
            irange(swapchain->MaxFrames)
            | vs::transform([&](auto) { return make_unique<Texture>(device, TextureArgs{ swapchain->CurrentTexture->Size, vk::Format::eR8G8B8A8Srgb, 1 }); })
            | to_vector
            ;
        {
            auto& io = ImGui::GetIO();
            io.Fonts->AddFontFromFileTTF(Assets / "bfont.ttf", 18);
        }

        auto ctx = ShaderContext();
        auto tree = ShaderTree(&ctx);
        

        while (!window.ShouldClose())
        {
            auto frame = frames[swapchain->CurrentIndex].get();
            frame->Begin();

            instanceImGui.OutputColor = imguiTextures[swapchain->CurrentIndex].get();
            instanceImGui.OutputColorClearValue = 0_xyzw;
            instanceImGui.Begin(frame);
            {
                using namespace ImGui;
                auto& io = GetIO();

                SetCurrentEditor(tree.Editor);
                ShowNodeStyleEditor();

                if (Begin("Nodes"))
                {
                    Text("FPS: %.2f (%.2gms)", io.Framerate, 1000 / io.Framerate);
                    Separator();

                    tree.OnGui();

                }
                End();

                ShowStyleEditor();

                if (Begin("Code"))
                {

                    
                }
                End();

                //ShowDemoWindow();
            }
            instanceImGui.End();

            frame->End();

            swapchain->Present(instanceImGui.OutputColor);

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
