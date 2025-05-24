#include "NodeShader.hpp"

#include "Kaey/ImGui.hpp"

#include "imgui_internal.h"

namespace Kaey
{
    namespace
    {
        template<class T, size_t... I>
        string_view NameOfVariant(size_t id, std::index_sequence<I...>)
        {
            constexpr string_view names[]{ (I, NAMEOF_SHORT_TYPE(typename T::template Type<I>))... };
            return names[id];
        }

        template<class T>
        string_view NameOfVariant(size_t id)
        {
            return NameOfVariant<T>(id, std::make_index_sequence<std::tuple_size_v<typename T::Types>>());
        }

    }

}

namespace ax::NodeEditor
{
    static bool Link(const void* id, const void* startPinId, const void* endPinId, const ImVec4& color = ImVec4(1, 1, 1, 1), float thickness = 1.0f)
    {
        assert(id != startPinId && startPinId != endPinId);
        return Link((LinkId)id, (PinId)startPinId, (PinId)endPinId, color, thickness);
    }

}

namespace Kaey::Renderer::Shader
{

    namespace
    {
        template<class Variant, class Visitor>
            requires std::is_base_of_v<VariantTag, Variant>
        auto operator|(Variant* ptr, Visitor&& vis) -> decltype(auto)
        {
            return Dispatch(ptr, std::forward<Visitor>(vis));
        }

        void SetNodePosition(Node* n, const ImVec2& v)
        {
            return ed::SetNodePosition(ed::NodeId(n), v);
        }

        void BeginPin(const void* id, ed::PinKind kind)
        {
            return BeginPin((ed::PinId)id, kind);
        }

        enum class PinType
        {
            Flow,
            Bool,
            Int,
            Float,
            String,
            Object,
            Function,
            Delegate,
        };

        ImColor GetIconColor(IType* type)
        {
            return Dispatch<ImColor>(type,
                [](IntType*) -> ImColor { return { 68, 201, 156 }; },
                [](FloatType*) -> ImColor { return 0xFFA1A1A1; },
                [](VectorType* ty) -> ImColor { return ty->Count == 4 ? 0xFF29C7C7 : 0xFFC76363; },
                [](IType*) -> ImColor { return 0xFFFFFFFF; }
            );
            //switch (type->KindId())
            //{
            //case PinType::Flow:     return { 255, 255, 255 };
            //case PinType::Bool:     return { 220,  48,  48 };
            //case PinType::Int:      return {  68, 201, 156 };
            ////case PinType::Float:    return { 147, 226,  74 };
            //case PinType::Float:    return { 161, 161, 161 };
            //case PinType::String:   return { 124,  21, 153 };
            //case PinType::Object:   return {  51, 150, 215 };
            //case PinType::Function: return { 218,   0, 183 };
            //case PinType::Delegate: return { 255,  48,  48 };
            //default: throw std::invalid_argument("type");
            //}
        };

        void DrawPinIcon(IType* type, bool connected, f32 alpha, f32 size = 20)
        {
            using ax::Drawing::IconType;
            ImColor color = GetIconColor(type);
            color.Value.w = alpha;
            auto iconType = Dispatch<IconType>(type,
                [](IntType*) { return IconType::Diamond; },
                [](FloatType*) { return IconType::Circle; },

                [](IType*) { return IconType::Circle; }
            );
            //auto iconType = [=]
            //{
            //    switch (type)
            //    {
            //    case PinType::Flow:     return Drawing::IconType::Flow;
            //    case PinType::Delegate: return Drawing::IconType::Square;
            //    case PinType::Bool:     return Drawing::IconType::Circle;
            //    case PinType::Int:      return Drawing::IconType::Circle;
            //    case PinType::Float:    return Drawing::IconType::Circle;
            //    case PinType::String:   return Drawing::IconType::Circle;
            //    case PinType::Object:   return Drawing::IconType::Circle;
            //    case PinType::Function: return Drawing::IconType::Circle;
            //    default: throw std::invalid_argument("type");
            //    }
            //}();
            ax::Widgets::Icon({ size, size }, iconType, connected, ImColor(0, 0, 0), color);
        };

        ImRect GetItemRect()
        {
            return { ImGui::GetItemRectMin(), ImGui::GetItemRectMax() };
        }

        template<class Variant>
            requires std::is_base_of_v<VariantTag, Variant>
        bool Combo(const char* label, size_t* id, size_t end)
        {
            return Combo(label, (int*)id, [](void*, int i, const char** out) -> bool { return *out = Kaey::NameOfVariant<Variant>(size_t(i)).data(); }, nullptr, (int)end);
        }

        template<class Variant>
            requires std::is_base_of_v<VariantTag, Variant>
        bool Combo(const char* label, size_t* id)
        {
            return Combo(label, id, std::tuple_size_v<typename Variant::Types>);
        }

        bool DragValue(string_view identifier, i32* value)
        {
            using namespace ImGui;
            PushID(value);
            auto result = DragInt("", value, 1, 0, 0, "{} %i"_f(identifier).data());
            PopID();
            return result;
        }

        bool DragValue(string_view identifier, f32* value)
        {
            using namespace ImGui;
            PushID(value);
            auto result = DragFloat("", value, .1f, 0, 0, "{} %.3f"_f(identifier).data());
            //auto result = SliderFloat("", value, 0, 1, "{} %.3f"_f(identifier).data());
            PopID();
            return result;
        }

        bool ColorButton(string_view id, const Vector4& col, ImGuiColorEditFlags flags, const Vector2& size)
        {
            return ImGui::ColorButton(id.data(), reinterpret_cast<const ImVec4&>(col), flags, reinterpret_cast<const ImVec2&>(size));
        }

        void BeginLine()
        {
            using namespace ImGui;
            BeginGroup();
            auto& pad = ed::GetStyle().NodePadding;
            if (pad.x < 0)
            {
                Dummy({ -pad.x, 0 });
                SameLine();
            }
        }

        void EndLine()
        {
            using namespace ImGui;
            auto& pad = ed::GetStyle().NodePadding;
            if (pad.z < 0)
            {
                SameLine();
                Dummy({ -pad.z, 0 });
            }
            EndGroup();
        }

    }

    struct NodeVisitorBase
    {
        NodeVisitorBase(ShaderTree* st, Node* n) : st(st), n(n)
        {

        }

    protected:
        ShaderTree* st;
        Node* n;
    };

    struct NameOf final : IExpression::Visitor<string>
    {
        string Visit(nullptr_t) const
        {
            return "nullptr_t";
        }

        string Visit(IExpression*) const
        {
            return "IExpression";
        }

        string Visit(IntExpression*) const
        {
            return "Int Value";
        }

        string Visit(FloatExpression*) const
        {
            return "Float Value";
        }

        string Visit(VectorExpression*) const
        {
            return "Vector Value";
        }

    };

    struct OnCreateVisitor final : NodeVisitorBase, IExpression::Visitor<void>
    {
        using NodeVisitorBase::NodeVisitorBase;

        void Visit(IExpression* e) const
        {

        }

        void Visit(IntExpression* e) const
        {
            st->AddOutput(n, "Value", e->Type);
        }

        void Visit(FloatExpression* e) const
        {
            st->AddOutput(n, "Value", e->Type);
        }

        void Visit(VectorExpression* e) const
        {
            st->AddOutput(n, "Value", e->Type);
        }

        void Visit(FunctionCallExpression* e) const
        {
            st->AddOutput(n, "Value", e->Type);
            for (auto [i, arg] : e->Arguments | indexed32)
				st->AddInput(n, "_{}"_f(i + 1), arg->Type);
        }

    };

    struct OnDestroyVisitor final : NodeVisitorBase, IExpression::Visitor<void>
    {
        using NodeVisitorBase::NodeVisitorBase;

        void Visit(nullptr_t) const
        {
            
        }

        void Visit(IntExpression* e) const
        {
            e->Context->DestroyExpression(e);
        }

        void Visit(FloatExpression* e) const
        {
            e->Context->DestroyExpression(e);
        }

        void Visit(VectorExpression* e) const
        {
            e->Context->DestroyExpression(e);
        }

    };

    struct OnGuiVisitor final : NodeVisitorBase, IExpression::Visitor<void>
    {
        using NodeVisitorBase::NodeVisitorBase;

        void Visit(nullptr_t) const
        {
            using namespace ImGui;

            auto name = !n->Name.empty() ? n->Name : n->Expression | NameOf();
            auto [tx, ty] = CalcTextSize(name);

            Spacing();
            {
                BeginLine();
                PushID(&n->Open);
                TextUnformatted(name);
                PopID();
                //if (IsItemClicked())
                //    Open = !Open;
                if (f32 width = 100; tx < width)
                {
                    Dummy({ width - tx, ty });
                    tx = width;
                }
                EndLine();
            }

            Spacing();
            if (!n->Open)
                return;

            f32 inputOffset = tx;
            for (auto in : n->Inputs)
                inputOffset = std::max(inputOffset, CalcTextSize(in->Name).x);
            for (auto& out : n->Outputs)
            {
                Dummy({ inputOffset - (CalcTextSize(out->Name).x + 0) + 20, 0 });
                SameLine();
                out->OnGui();
            }
            Spacing();
            for (auto& in : n->Inputs)
                in->OnGui();

        }

        void Visit(IExpression* e) const
        {
            Visit(nullptr);
        }

        void Visit(IntExpression* e) const
        {
            using namespace ImGui;
            Visit(nullptr);
            if (!n->Open)
                return;
            auto w = GetItemRect().GetWidth();

            BeginLine();
            SetNextItemWidth(w);
            DragValue("Value", &e->Value);
            EndLine();

            Spacing();
            //[&](VectorType* ty) { ty->Count == 4 ? ColorPicker4("", ColorValue.raw, ImGuiColorEditFlags_None) : DragFloat3("", VectorValue.raw); }
        }

        void Visit(FloatExpression* e) const
        {
            using namespace ImGui;
            Visit(nullptr);
            if (!n->Open)
                return;
            auto w = GetItemRect().GetWidth() + 35;
            BeginLine();
            SetNextItemWidth(w);
            DragValue({}, &e->Value);
            EndLine();
            Spacing();
        }

        void Visit(VectorExpression* e) const
        {
            using namespace ImGui;
            Visit(nullptr);
            if (!n->Open)
                return;
            auto w = GetItemRect().GetWidth() + 40;
            for (auto [i, str] : array{ "X", "Y", "Z" } | indexed32)
            {
                BeginLine();
                SetNextItemWidth(w);
                DragValue(str, &e->Value[i]);
                EndLine();
            }
        }

    private:
        vector<pair<f32, IType*>> outputs;

        void BeginOutput(string_view name, IType* type)
        {
            using namespace ImGui;
            BeginPin((ed::PinId)this, ed::PinKind::Output);
            {
                BeginGroup();
                AlignTextToFramePadding();
                TextUnformatted(name);
                EndGroup();

                auto r1 = GetItemRect();

                SameLine();

                DrawPinIcon(type, false, 1);
                auto r2 = GetItemRect();

                ed::PinPivotAlignment({ (r1.GetWidth() + r2.GetWidth() * .5f) / (r1.GetWidth() + r2.GetWidth()), r2.GetHeight() * .5f / std::max(r1.GetHeight(), r2.GetHeight()) });
            }
        }

        void EndOutput()
        {
            ed::EndPin();
        }

    };

    NodeIO::NodeIO(Node* parent) : Parent(parent)
    {
        
    }

    NodeInput::NodeInput(Node* parent, string name, IType* type, NodeOutput* connection) :
        ParentClass(parent),
        Name(move(name)),
        Type(type),
        Connection(connection),
        HideValue(false)
    {

    }

    void NodeInput::OnGui()
    {
        using namespace ImGui;
        BeginPin((ed::PinId)this, ed::PinKind::Input);
        {
            DrawPinIcon(Type, false, 1);
            auto r1 = GetItemRect();
            SameLine();
            BeginGroup();
            if (HideValue)
            {
                AlignTextToFramePadding();
                TextUnformatted(Name);
            }
            else Dispatch(Type,
                [&](VectorType* ty)
                {
                    if (ty->Count == 4)
                    {
                        AlignTextToFramePadding();
                        TextUnformatted(Name);
                        SameLine();
                        auto str = "{}"_f((void*)&VectorValue);
                        auto str2 = "##" + str;
                        auto str3 = "###" + str;
                        if (ColorButton(str, ColorValue, 0, { 0, 0 }))
                            OpenPopup(str2.data());
                        if (BeginPopup(str2.data()))
                        {
                            ColorPicker4(str3.data(), ColorValue.raw, ImGuiColorEditFlags_None, nullptr);
                            EndPopup();
                        }
                    }
                },
                [&](FloatType*)
                {
                    SetNextItemWidth(100);
                    DragValue(Name, &FloatValue);
                }
            );
            EndGroup();
            auto r2 = GetItemRect();
            ed::PinPivotAlignment({ r1.GetWidth() * .5f / (r1.GetWidth() + r2.GetWidth()), r1.GetHeight() * .5f / std::max(r1.GetHeight(), r2.GetHeight()) });
        }
        ed::EndPin();
    }

    NodeOutput::NodeOutput(Node* parent, string name, IType* type) :
        ParentClass(parent),
        Name(move(name)),
        Type(type)
    {

    }

    void NodeOutput::OnGui()
    {
        using namespace ImGui;
        BeginPin((ed::PinId)this, ed::PinKind::Output);
        {
            BeginGroup();
            AlignTextToFramePadding();
            TextUnformatted(Name);
            EndGroup();

            auto r1 = GetItemRect();

            SameLine();

            DrawPinIcon(Type, false, 1);
            auto r2 = GetItemRect();

            ed::PinPivotAlignment({ (r1.GetWidth() + r2.GetWidth() * .5f) / (r1.GetWidth() + r2.GetWidth()), r2.GetHeight() * .5f / std::max(r1.GetHeight(), r2.GetHeight()) });
        }
        ed::EndPin();
    }

    Node::Node(string name) :
        Name(move(name)),
        Open(true),
        Expression(nullptr)
    {

    }

    ShaderTree::ShaderTree(ShaderContext* ctx):
        ctx(ctx),
        currentStage(0)
    {
        config.NavigateButtonIndex = 2;
        editor.reset(CreateEditor(&config));

        //{
        //    auto n = &nodes.emplace_back("Principled BSDF");
        //
        //    AddInput(n, "Base Color", ctx->VectorTy[ctx->FloatTy][4]);
        //    AddInput(n, "Metallic", ctx->FloatTy);
        //    AddInput(n, "Roughness", ctx->FloatTy);
        //    AddInput(n, "IOR", ctx->FloatTy);
        //    AddInput(n, "Alpha", ctx->FloatTy);
        //    AddInput(n, "Normal", ctx->VectorTy[ctx->FloatTy][3])->HideValue = true;
        //
        //    AddOutput(n, "BSDF", ctx->VectorTy[ctx->FloatTy][4]);
        //
        //    SetCurrentEditor(editor.get());
        //    SetNodePosition(n, { -500, -400 });
        //    ed::SetCurrentEditor(nullptr);
        //}

    }

    NodeInput* ShaderTree::AddInput(Node* parent, string name, IType* type)
    {
        auto ptr = make_unique<NodeInput>(parent, move(name), type, nullptr);
        auto res = ptr.get();
        pins.emplace_back(move(ptr));
        parent->Inputs.emplace_back(res);
        return res;
    }

    NodeOutput* ShaderTree::AddOutput(Node* parent, string name, IType* type)
    {
        auto ptr = make_unique<NodeOutput>(parent, move(name), type);
        auto res = ptr.get();
        pins.emplace_back(move(ptr));
        parent->Outputs.emplace_back(res);
        return res;
    }

    void ShaderTree::OnGui()
    {
        using namespace ImGui;

        SetCurrentEditor(editor.get());
        ed::Begin("My Editor");

        for (auto node : nodes | addressOf)
        {
            BeginNode(ed::NodeId(node));
            node->Expression | OnGuiVisitor(this, node);
            ed::EndNode();
        }

        for (auto& link : links)
            ed::Link(&link, link.Input, link.Output);

        if (ed::BeginCreate())
            OnCreateLink();
        ed::EndCreate();

        if (ed::BeginDelete())
        {
            OnDeleteLink();
            OnDeleteNode();
        }
        ed::EndDelete();

        auto mousePos = GetMousePos();

        ed::Suspend();
        if (ed::ShowBackgroundContextMenu())
        {
            OpenPopup("Create New Node");
            openPopupPosition = mousePos;
        }

        if (BeginPopup("Create New Node"))
        {
            if (MenuItem("Int Value"))
            {
                auto n = CreateNodeFromExpression(ctx->CreateExpression<IntExpression>());
                SetNodePosition(n, openPopupPosition);
            }

            if (MenuItem("Float Value"))
            {
                auto n = CreateNodeFromExpression(ctx->CreateExpression<FloatExpression>());
                SetNodePosition(n, openPopupPosition);
            }

            if (MenuItem("Vector Value"))
            {
                auto n = CreateNodeFromExpression(ctx->CreateExpression<VectorExpression>());
                SetNodePosition(n, openPopupPosition);
            }

            if (MenuItem("Normal Map"))
            {
                auto args = vector<IExpression*>
                {
                    ctx->CreateExpression<FloatExpression>(),
                    ctx->CreateExpression<ColorExpression>(),
                };
                auto n = CreateNodeFromExpression(ctx->CreateExpression<FunctionCallExpression>("NormalMap", move(args), ctx->Vector3Ty));
                SetNodePosition(n, openPopupPosition);
            }

            if (MenuItem("Principled BSDF"))
            {
                auto args = vector<IExpression*>
            	{
            		ctx->CreateExpression<ColorExpression>(), //Diffuse
                    ctx->CreateExpression<FloatExpression>(), //Metallic
                    ctx->CreateExpression<FloatExpression>(), //Roughness
                    ctx->CreateExpression<FloatExpression>(), //IOR
                    ctx->CreateExpression<FloatExpression>(), //Alpha
                    ctx->CreateExpression<VectorExpression>(), //Normal
            	};
                auto n = CreateNodeFromExpression(ctx->CreateExpression<FunctionCallExpression>("PrincipledBSDF", move(args), ctx->ColorTy));
                SetNodePosition(n, openPopupPosition);
            }

            if (MenuItem("Shader Output"))
            {
                auto n = &nodes.emplace_back();
                n->Expression = nullptr;
                //e | OnCreateVisitor(this, n);
                AddInput(n, "Diffuse", ctx->GetVectorTy(ctx->FloatTy, 4))->HideValue = true;
                SetNodePosition(n, openPopupPosition);
            }

            EndPopup();
        }
        ed::Resume();

        ed::End();
        ed::SetCurrentEditor(nullptr);
    }

    void ShaderTree::OnCreateLink()
    {
        ed::PinId startId, endId;
        if (!QueryNewLink(&startId, &endId) || !startId || !endId)
            return;
        auto start = startId.AsPointer<NodeIO>();
        auto end = endId.AsPointer<NodeIO>();
        if (start->KindId() == end->KindId() || start->Parent == end->Parent)
            return;
        auto [input, output] = VariadicVisit<pair<NodeInput*, NodeOutput*>>({ start, end },
            [&](NodeInput* i, NodeOutput* o) { return pair(i, o); },
            [&](NodeOutput* o, NodeInput* i) { return pair(i, o); }
        );
        if (!ed::AcceptNewItem())
            return;
        if (input->Connection)
            erase_if(links, [=](auto& link) { return link.Input == input; });
        auto& link = links.emplace_back(input, output);
        input->Connection = output;
        ed::Link(&link, input, output);
    }

    void ShaderTree::OnDeleteLink()
    {
        ed::LinkId linkId;
        while (QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
                erase_if(links, [=](auto& link) { return &link == linkId.AsPointer(); });
            //ed::RejectDeletedItem();
        }
    }

    void ShaderTree::OnDeleteNode()
    {
        ed::NodeId nodeId;
        while (QueryDeletedNode(&nodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                auto n = nodeId.AsPointer<Node>();
                n->Expression | OnDestroyVisitor(this, n);
                erase_if(nodes, [=](auto& node) { return &node == n; });
            }
        }
    }

    Node* ShaderTree::CreateNodeFromExpression(IExpression* e)
    {
        auto n = &nodes.emplace_back();
        n->Expression = e;
        e | OnCreateVisitor(this, n);
        return n;
    }

}
