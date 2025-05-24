#include "pch.hpp"

#include <imgui.cpp>
#include <imgui_draw.cpp>
#include <imgui_demo.cpp>
#include <imgui_tables.cpp>
#include <imgui_widgets.cpp>

#include <imgui_impl_glfw.cpp>
#include <imgui_impl_vulkan.cpp>

#include <crude_json.cpp>
#include <imgui_canvas.cpp>
#include <imgui_node_editor.cpp>
#include <imgui_node_editor_api.cpp>

#include <imnodes.cpp>

#include <GraphEditor.cpp>
#include <ImCurveEdit.cpp>
#include <ImGradient.cpp>
#include <ImGuizmo.cpp>
#include <ImSequencer.cpp>

template struct linm::VectorN<half_float::half, 2>;
template struct linm::VectorN<half_float::half, 3>;
template struct linm::VectorN<half_float::half, 4>;
template struct linm::VectorN<int32_t, 2>;
template struct linm::VectorN<int32_t, 3>;
template struct linm::VectorN<int32_t, 4>;
template struct linm::VectorN<uint32_t, 2>;
template struct linm::VectorN<uint32_t, 3>;
template struct linm::VectorN<uint32_t, 4>;
