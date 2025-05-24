#pragma once
#include "Utils.hpp"

namespace Kaey::Engine
{
    enum class CameraMode
    {
        Perspective,
        Orthographic
    };

    struct Scene
    {
        Scene(Engine::RenderDevice* renderDevice);
        Scene(Engine::Project* project);

        Scene(const Scene&) = delete;
        Scene(Scene&&) noexcept = delete;

        Scene& operator=(const Scene&) = delete;
        Scene& operator=(Scene&&) noexcept = delete;

        ~Scene();

        LightObject* CreateLight();

        CameraObject* CreateCamera();

        void OnUpdate();

        void Render();

        void OnGui();

        const Vector4& GetAmbientColor() const;
        void SetAmbientColor(const Vector4& color);

        void AddGameObject(unique_ptr<GameObject> go);

        void Load(const fs::path& path);

        void Load(const json& j);

        void Save(json& j) const;

        void Save(const fs::path& path) const;

        GameObject* LoadGameObject(const json& j, GameObject* parent = nullptr);

        GameObject* LoadGameObject(const fs::path& path, GameObject* parent = nullptr);

        void Register(GameObject* value);
        void UnRegister(GameObject* value);

        KAEY_ENGINE_GETTER(Engine::RenderDevice*, RenderDevice) { return renderDevice; }
        KAEY_ENGINE_GETTER(Engine::Project*, Project) { return project; }
        KAEY_ENGINE_GETTER(KaeyEngine*, Engine);
        KAEY_ENGINE_GETTER(Engine::RenderEngine*, RenderEngine);
        KAEY_ENGINE_GETTER(Engine::ThreadPool*, ThreadPool);
        KAEY_ENGINE_GETTER(Engine::Time*, Time);

        KAEY_ENGINE_PROPERTY(Vector4, AmbientColor);

        KAEY_ENGINE_GETTER(cspan<GameObject*>, GameObjects) { return gameObjects; }
        KAEY_ENGINE_GETTER(cspan<MeshObject*>, MeshObjects) { return meshObjects; }
        KAEY_ENGINE_GETTER(cspan<LightObject*>, Lights) { return lightObjects; }
        KAEY_ENGINE_GETTER(cspan<CameraObject*>, Cameras) { return cameraObjects; }
        KAEY_ENGINE_GETTER(GameObject*, ActiveObject) { return activeObject; }

    private:
        Engine::RenderDevice* renderDevice;
        Engine::Project* project;
        Vector4 ambientColor;

        vector<unique_ptr<GameObject>> gameObjectPtrs;

        vector<GameObject*> gameObjects;
        vector<MeshObject*> meshObjects;
        vector<LightObject*> lightObjects;
        vector<CameraObject*> cameraObjects;

        mutex objectMutex;

        //ImGui
        GameObject* activeObject = nullptr;
    };

    struct GameObject : Kaey::Variant<GameObject,
        LightObject,
        CameraObject,
        MeshObject
    >
    {
        explicit GameObject(Scene* scene);

        GameObject(const GameObject&) = delete;
        GameObject(GameObject&&) noexcept = delete;

        GameObject& operator=(const GameObject&) = delete;
        GameObject& operator=(GameObject&&) noexcept = delete;

        virtual ~GameObject() noexcept;

        string_view GetName() const;
        void SetName(string value);

        GameObject* GetParent() const;
        void SetParent(GameObject* value);

        void AddChild(GameObject* child);

        const Vector3& GetPosition() const { return position; }
        void SetPosition(const Vector3& value);
        const Quaternion& GetRotation() const { return rotation; }
        void SetRotation(const Quaternion& value);
        const Vector3& GetScale() const { return scale; }
        void SetScale(const Vector3& value);

        virtual void OnTransformChange();

        virtual void Load(const json& j);
        virtual void Save(json& j) const;

        json Save() const;

        virtual void OnGui();

        KAEY_ENGINE_GETTER(Engine::Scene*, Scene) { return scene; }

        KAEY_ENGINE_GETTER(Engine::RenderDevice*, RenderDevice) { return Scene->RenderDevice; }
        KAEY_ENGINE_GETTER(Engine::Project*, Project) { return Scene->Project; }
        KAEY_ENGINE_GETTER(KaeyEngine*, Engine) { return Scene->Engine; }
        KAEY_ENGINE_GETTER(Engine::RenderEngine*, RenderEngine) { return Scene->RenderEngine; }
        KAEY_ENGINE_GETTER(Engine::ThreadPool*, ThreadPool) { return Scene->ThreadPool; }
        KAEY_ENGINE_GETTER(Engine::Time*, Time) { return Scene->Time; }

        KAEY_ENGINE_PROPERTY(string_view, Name);

        KAEY_ENGINE_PROPERTY(GameObject*, Parent);
        KAEY_ENGINE_ARRAY_PROPERTY(GameObject*, Child);
        KAEY_ENGINE_GETTER(cspan<GameObject*>, Children) { return children; }

        KAEY_ENGINE_PROPERTY(Vector3, Position);
        KAEY_ENGINE_PROPERTY(Quaternion, Rotation);
        KAEY_ENGINE_PROPERTY(Vector3, Scale);

        KAEY_ENGINE_GETTER(const Matrix4&, TransformMatrix);
        KAEY_ENGINE_GETTER(const Matrix4&, NormalMatrix);

    private:
        Engine::Scene* scene;
        GameObject* parent;
        string name;
        vector<GameObject*> children;
        unique_ptr<mutex> childrenMutex;
        Vector3 position;
        Quaternion rotation;
        Vector3 scale;
        mutable optional<Matrix4> trMatrix;
        mutable optional<Matrix4> normalMatrix;
        //ImGui
        Vector3 eulerRotation;
        bool lockScale = true;
    };

    struct LightObject final : GameObject::With<LightObject>
    {
        explicit LightObject(Engine::Scene* scene);

        void OnTransformChange() override;
        void OnGui() override;

        void Load(const json& j) override;
        void Save(json& j) const override;

        Vector4 GetColor() const { return lightData.Color; }
        void SetColor(Vector4 color) { lightData.Color = color; }

        const UniformLight& GetLightData() const { return lightData; }

        KAEY_ENGINE_READONLY_PROPERTY(const UniformLight&, LightData);
        KAEY_ENGINE_PROPERTY(Vector4, Color);

    private:
        Engine::UniformLight lightData;
    };

    struct CameraObject final : GameObject::With<CameraObject>
    {
        explicit CameraObject(Engine::Scene* scene);

        void OnTransformChange() override;
        void Load(const json& j) override;
        void Save(json& j) const override;
        void OnGui() override;

        float GetFov() const { return fov; }
        void SetFov(float value);

        float GetFar() const { return far; }
        void SetFar(float value);

        float GetNear() const { return near; }
        void SetNear(float value);

        CameraMode GetCameraMode() const { return cameraMode; }
        void SetCameraMode(CameraMode value);

        KAEY_ENGINE_PROPERTY(float, Fov);
        KAEY_ENGINE_PROPERTY(float, Far);
        KAEY_ENGINE_PROPERTY(float, Near);
        KAEY_ENGINE_GETTER(Matrix4, ViewMatrix);
        KAEY_ENGINE_GETTER(Matrix4, ProjectionMatrix);
        KAEY_ENGINE_GETTER(Engine::Frame*, Frame) { return frame.get(); }
        KAEY_ENGINE_PROPERTY(CameraMode, CameraMode);
        KAEY_ENGINE_GETTER(shared_ptr<Texture>&, TargetTexture) { return targetTexture; }
        KAEY_ENGINE_GETTER(shared_ptr<Texture>&, TargetDepthTexture) { return targetDepthTexture; }

    private:
        float fov;
        float far;
        float near;
        Engine::CameraMode cameraMode;
        mutable optional<Matrix4> viewMatrix;
        mutable optional<Matrix4> projectionMatrix;
        unique_ptr<Engine::Frame> frame;
        mutable shared_ptr<Texture> targetTexture;
        mutable shared_ptr<Texture> targetDepthTexture;
    };

    struct MeshModifier
    {
        virtual ~MeshModifier() = default;
        virtual void OnAdd(MeshObject* mesh) = 0;
        virtual void OnUpdate() = 0;
        virtual void OnRemove() = 0;
        virtual void OnGui() = 0;
        virtual string_view ModifierName() = 0;
        virtual void Load(const json& j) = 0;
        virtual void Save(json& j) = 0;
    };

    enum class AttributeType : int
    {
        Float,
        Float2,
        Float3,
        Float4,
        I32,
        U32,
    };

    struct VertexAttribute
    {
        AttributeType Type;
        string Name;
        u64 Offset;
        KAEY_ENGINE_GETTER(u32, TypeSize);
    };

    struct SurfaceDeformModifier;
    struct DisplaceModifier;

    struct MeshObject final : GameObject::With<MeshObject>
    {
        friend SurfaceDeformModifier;
        friend DisplaceModifier;

        MeshObject(Engine::Scene* scene);
        MeshObject(Engine::Scene* scene, shared_ptr<Engine::MeshData> meshData, vector<shared_ptr<Material>> mats);

        MeshObject(const MeshObject&) = delete;
        MeshObject(MeshObject&&) noexcept = delete;

        MeshObject& operator=(const MeshObject&) = delete;
        MeshObject& operator=(MeshObject&&) noexcept = delete;

        ~MeshObject() override;

        u32 GetShapeIndex() const;
        void SetShapeIndex(u32 value);

        bool GetLockShape() const;
        void SetLockShape(bool value);

        float GetShapeValues(u32 i) const;
        void SetShapeValues(u32 i, float value);

        void OnTransformChange() override;
        void Load(const json& j) override;
        void Save(json& j) const override;
        void OnGui() override;

        void Update();
        void UpdateTBN();

        span<shared_ptr<Material>> GetMaterials() { return materials; }

        MeshModifier* GetModifiers(i32 i) const { return modifiers[i].get(); }
        void AddModifier(unique_ptr<MeshModifier> modifier);
        unique_ptr<MeshModifier> RemoveModifier(MeshModifier* modifier);

        MeshModifier* GetDependents(i32 i) const { return dependents[i]; }
        void AddDependent(MeshModifier* mod);
        void RemoveDependent(MeshModifier* mod);

        const VertexAttribute& AddVertexAttribute(AttributeType type, string name);

        const shared_ptr<Engine::MeshData>& GetMeshData() const { return meshData; }
        void SetMeshData(const shared_ptr<Engine::MeshData>& value);

        KAEY_ENGINE_PROPERTY(const shared_ptr<Engine::MeshData>&, MeshData);

        KAEY_ENGINE_GETTER(DefinedMemoryBuffer<Vertex>*, VertexBuffer) { return vertexBuffer.get(); }

        KAEY_ENGINE_GETTER(cspan<shared_ptr<Engine::Material>>, Materials) { return materials; }

        KAEY_ENGINE_PROPERTY(u32, ShapeIndex);
        KAEY_ENGINE_PROPERTY(bool, LockShape);
        KAEY_ENGINE_ARRAY_PROPERTY(float, ShapeValues);
        KAEY_ENGINE_READONLY_ARRAY_PROPERTY(MeshModifier*, Modifiers);
        KAEY_ENGINE_READONLY_ARRAY_PROPERTY(MeshModifier*, Dependents);
        KAEY_ENGINE_GETTER(cspan<VertexAttribute>, VertexAttributes) { return vertexAttributes; }

        KAEY_ENGINE_GETTER(u32, UvIndex) { return uvIndex; }

    private:
        shared_ptr<Engine::MeshData> meshData;
        unique_ptr<DefinedMemoryBuffer<Vertex>> vertexBuffer;
        unique_ptr<DefinedMemoryBuffer<float>> shapeDeltasBuffer;
        unique_ptr<DefinedMemoryBuffer<TBNInfo>> tbnBuffer;
        unique_ptr<MemoryBuffer> vertexAttributeBuffer;
        vector<shared_ptr<Engine::Material>> materials;
        u32 shapeIndex;
        bool lockShape;
        bool updateRequired;
        vector<float> shapeValues;
        unique_ptr<ComputeData> shapeComputeData, faceTbnData, tbnData;
        vector<unique_ptr<MeshModifier>> modifiers;
        vector<MeshModifier*> dependents;
        vector<VertexAttribute> vertexAttributes;

        u32 uvIndex;

        //ImGui
        int modIndex = 0;
        AttributeType attributeType = AttributeType::Float;
        string attName = "Attribute1";

    public:
        float metallic = 0;
        float roughness = .5f;
    };

    enum class BindingStatus
    {
        UnBound,
        Binding,
        Bound,
    };

    struct SurfaceDeformModifier final : MeshModifier
    {
        SurfaceDeformModifier(RenderDevice* device);

        MeshObject* GetTarget() const { return target; }
        void SetTarget(MeshObject* value);

        void OnAdd(MeshObject* mesh) override;
        void OnUpdate() override;
        void OnRemove() override;
        void OnGui() override;
        string_view ModifierName() override;
        void Load(const json& j) override;
        void Save(json& j) override;

        void Bind();
        void UnBind();

        KAEY_ENGINE_PROPERTY(MeshObject*, Target);
        KAEY_ENGINE_GETTER(BindingStatus, Status) { return status; }

    private:
        RenderDevice* device;
        MeshObject* mesh;
        MeshObject* target;
        BindingStatus status;
        unique_ptr<DefinedMemoryBuffer<VertexBinding>> bindBuffer;
        unique_ptr<ComputeData> deformData;
        //ImGui
        int targetIdx = 0;
        float maxDistance = .5f;
    };

    struct DisplaceModifier final : MeshModifier
    {
        DisplaceModifier(RenderDevice* renderDevice);

        void OnAdd(MeshObject* mesh) override;
        void OnUpdate() override;
        void OnRemove() override;
        void OnGui() override;
        string_view ModifierName() override;
        void Load(const json& j) override;
        void Save(json& j) override;

        float GetValue() const;
        void SetValue(float value);

        KAEY_ENGINE_PROPERTY(float, Value);
        KAEY_ENGINE_GETTER(Engine::RenderDevice*, RenderDevice) { return renderDevice; }
    private:
        Engine::RenderDevice* renderDevice;
        MeshObject* mesh;
        unique_ptr<ComputeData> displaceData;
        float value;
    };

}
