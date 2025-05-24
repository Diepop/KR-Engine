#pragma once
#include "Shader.hpp"

namespace Kaey::Renderer::Shader
{
    using EditorContextUnique = unique_ptr<ax::NodeEditor::EditorContext, decltype([](auto ptr) { ax::NodeEditor::DestroyEditor(ptr); })>;

    namespace ed = ax::NodeEditor;

    struct OnCreateVisitor;
    struct OnDestroyVisitor;
    struct OnGuiVisitor;

    struct Node;

    struct NodeIO : Variant<NodeIO,
        struct NodeInput,
        struct NodeOutput
    >
    {
        Node* Parent;

        explicit NodeIO(Node* parent);

        KR_NO_COPY_MOVE(NodeIO);

        virtual ~NodeIO() = default;
    };

    struct NodeInput final : NodeIO::With<NodeInput>
    {
        string Name;
        IType* Type;
        NodeOutput* Connection;
        f32 FloatValue = 0;
        Vector4 VectorValue;
        Vector4 ColorValue = 1_xyzw;
        bool HideValue;

        NodeInput(Node* parent, string name, IType* type, NodeOutput* connection);

        KR_NO_COPY_MOVE(NodeInput);

        void OnGui();

    };

    struct NodeOutput final : NodeIO::With<NodeOutput>
    {
        string Name;
        IType* Type;

        NodeOutput(Node* parent, string name, IType* type);

        KR_NO_COPY_MOVE(NodeOutput);

        void OnGui();

    };

    struct Node final
    {
        string Name;
        vector<NodeInput*> Inputs;
        vector<NodeOutput*> Outputs;
        bool Open;
        IExpression* Expression;

        explicit Node(string name = {});

        KR_NO_COPY_MOVE(Node);

    };

    struct Link
    {
        NodeInput* Input;
        NodeOutput* Output;
    };

    struct ShaderTree
    {
        friend struct OnCreateVisitor;
        friend struct OnGuiVisitor;

        explicit ShaderTree(ShaderContext* ctx);

        KR_NO_COPY(ShaderTree);

        KR_GETTER(ax::NodeEditor::EditorContext*, Editor) { return editor.get(); }

        NodeInput* AddInput(Node* parent, string name, IType* type);

        NodeOutput* AddOutput(Node* parent, string name, IType* type);

        void OnGui();

    private:
        ShaderContext* ctx;
        ax::NodeEditor::Config config;
        EditorContextUnique editor;
        size_t currentStage;
        list<Node> nodes;
        list<Link> links;
        list<unique_ptr<NodeIO>> pins;
        ImVec2 openPopupPosition;

        void OnCreateLink();

        void OnDeleteLink();

        void OnDeleteNode();

        Node* CreateNodeFromExpression(IExpression* e);

    };

}
