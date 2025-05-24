#include "Scene.hpp"

namespace Kaey::Engine
{
    namespace
    {
        const Quaternion CameraRotationDelta = { 1, 0, 0, 0 };

        const vector<pair<string_view, unique_ptr<MeshModifier>(*)(RenderDevice*)>> ModifierFactories
        {
            { "SurfaceDeform", +[](RenderDevice* dev) -> unique_ptr<MeshModifier> { return make_unique<SurfaceDeformModifier>(dev); } },
            { "Displace", +[](RenderDevice* dev) -> unique_ptr<MeshModifier> { return make_unique<DisplaceModifier>(dev); } },
        };
        
        const Vector4 DefaultAmbientColor = { 1, 1, 1, 0 };
    }
    
    Scene::Scene(Engine::RenderDevice* renderDevice) :
        renderDevice(renderDevice),
        project(nullptr),
        ambientColor(DefaultAmbientColor),
        uniformObjects(renderDevice->AllocateMemory<UniformObject>(MAX_NUM_OBJECTS, vk::BufferUsageFlagBits::eUniformBuffer)),
        uniformCameras(renderDevice->AllocateMemory<UniformCamera>(MAX_NUM_CAMERAS, vk::BufferUsageFlagBits::eUniformBuffer)),
        uniformLights(renderDevice->AllocateMemory<UniformLight>(MAX_NUM_LIGHTS, vk::BufferUsageFlagBits::eUniformBuffer))
    {

    }

    Scene::Scene(Engine::Project* project) : Scene(project->RenderDevice)
    {
        this->project = project;
    }

    Scene::~Scene() = default;

    LightObject* Scene::CreateLight()
    {
        auto ptr = make_unique<LightObject>(this);
        auto result = ptr.get();
        AddGameObject(move(ptr));
        return result;
    }

    CameraObject* Scene::CreateCamera()
    {
        auto ptr = make_unique<CameraObject>(this);
        auto result = ptr.get();
        AddGameObject(move(ptr));
        return result;
    }

    void Scene::OnUpdate()
    {
        auto l = lock_guard(objectMutex);
        for (auto mdl : MeshObjects)
            mdl->Update();
    }

    void Scene::Render()
    {
        auto l = lock_guard(objectMutex);
        auto objData =
            MeshObjects
            | vs::transform([](MeshObject* c) { return UniformObject{ c->NormalMatrix, c->TransformMatrix }; })
            ;
        auto camData =
            Cameras
            | vs::transform([](CameraObject* c) { return UniformCamera{ c->ProjectionMatrix, c->ViewMatrix, c->Position }; })
            ;
        auto lightData =
            Lights
            | vs::transform([](LightObject* l) { return UniformLight{ l->Position, l->Color }; })
            ;
        //UniformObjects->WriteData(objData);
        //UniformCameras->WriteData(camData);
        //UniformLights->WriteData(lightData);
        renderDevice->DiffusePipeline->ObjectBuffer->WriteData(objData);
        renderDevice->DiffusePipeline->CameraBuffer->WriteData(camData);
        renderDevice->DiffusePipeline->LightBuffer->WriteData(lightData);

        for (u32 cameraIndex = 0; cameraIndex < cameraObjects.size(); ++cameraIndex)
        {
            auto cam = cameraObjects[cameraIndex];
            PushObject push
            {
                .ObjectIndex = 0,
                .CameraIndex = cameraIndex,
                .LightCount = u32(lightObjects.size()),
                .AmbientColor = AmbientColor,
            };
            auto frame = cam->Frame;
            auto cmd = frame->CommandBuffer;
            frame->BeginRender(cam->TargetTexture.get(), cam->TargetDepthTexture.get());
            frame->BindPipeline(renderDevice->DiffusePipeline);
            for (auto model : MeshObjects)
            {
                push.UvIndex = model->UvIndex;
                push.TangentIndex = model->UvIndex + model->MeshData->VertexBuffer->Count;
                push.Roughness = model->roughness;
                push.Metallic = model->metallic;
                vk::DeviceSize offsets[]{ 0 };
                cmd.bindVertexBuffers(0, model->VertexBuffer->Instance, offsets);
                cmd.bindIndexBuffer(model->MeshData->IndexBuffer->Instance, 0, vk::IndexType::eUint32);
                for (auto& [matId, first, count] : model->MeshData->MaterialRanges)
                {
                    auto mat = !model->Materials.empty() ? model->Materials[matId] : nullptr;
                    if (!mat || mat->Pipeline == renderDevice->DiffusePipeline)
                    {
                        push.MaterialIndex = renderDevice->DiffusePipeline->IndexOf(mat.get());
                        if (mat)
                            push.AlphaClip = 1 - mat->AlphaClip;
                        cmd.pushConstants(renderDevice->DiffusePipeline->Layout->Instance, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof PushObject, &push);
                        cmd.drawIndexed(count, 1, first, 0, 0);
                    }

                }
                ++push.ObjectIndex;
            }

            frame->EndRender();
        }
    }

    void Scene::OnGui()
    {
        using namespace ImGui;
        unordered_map<string_view, i32> countMap;
        if (IsItemClicked())
            activeObject = nullptr;
        auto fn = [&](GameObject* go, auto&& f) -> void
        {
            auto flags =
                go->Children.size() == 0 ?
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet :
                ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
                ;
            if (go == activeObject)
                flags |= ImGuiTreeNodeFlags_Selected;
            auto count = countMap[go->Name]++;
            auto open = TreeNodeEx(go, flags, (count > 0 ? "{}.{:03}"_f(go->Name, count) : "{}"_f(go->Name)).data());
            if (IsItemClicked())
                activeObject = go;
            if (open)
            {
                for (auto child : go->Children)
                    f(child, f);
                TreePop();
            }
        };
        for (auto go : GameObjects) if (!go->Parent)
            fn(go, fn);
    }

    const Vector4& Scene::GetAmbientColor() const
    {
        return ambientColor;
    }

    void Scene::SetAmbientColor(const Vector4& color)
    {
        ambientColor = color;
    }

    void Scene::AddGameObject(unique_ptr<GameObject> go)
    {
        Register(go.get());
        Engine->SubmitSyncronized([ptr = go.release(), this] { gameObjectPtrs.emplace_back(ptr); });
    }

    void Scene::Load(const fs::path& path)
    {
        json j;
        {
            ifstream f(path);
            if (!f.is_open())
                throw runtime_error("Failed to open file: {}"_f(path.string()));
            f >> j;
        }
        Load(j);
    }

    void Scene::Load(const json& j)
    {
        auto it = j.find("Type");
        if (it == j.end())
            throw runtime_error("Asset type unspecified, Expected 'Scene'!");
        if (it->get<string>() != "Scene")
            throw runtime_error("Asset type is not 'Scene'!");

        it = j.find("AmbientColor");
        if (it != j.end() && it->is_array())
        {
            auto v = it->get<vector<float>>();
            AmbientColor = { v[0], v[1], v[2], v[3] };
        }

        it = j.find("Children");
        if (it == j.end() || !it->is_array())
            return;

        auto tasks = it->get<vector<json>>() | vs::transform([&](json& jj)
        {
            return RenderDevice->ThreadPool->Submit([=, this]
            {
                return LoadGameObject(jj);
            });
        }) | to_vector;
        
        for (auto& task : tasks)
            task.get();
    }

    void Scene::Save(json& j) const
    {
        j["Type"] = "Scene";
        if (AmbientColor != DefaultAmbientColor)
            j["AmbientColor"] = AmbientColor;
        j["Children"] = gameObjects | vs::filter([](auto go) { return !go->Parent; }) | vs::transform([](auto go) { return go->Save(); }) | to_vector;
    }

    void Scene::Save(const fs::path& path) const
    {
        auto f = ofstream(path);
        if (!f.is_open())
            throw runtime_error("Failed to save file '{}'"_f(path.string()));
        json j;
        Save(j);
        f << j.dump(2);
    }

    GameObject* Scene::LoadGameObject(const json& j, GameObject* parent)
    {
        auto it = j.find("Type");
        if (it == j.end())
            throw runtime_error("Prefab doesn't contain a 'Type' key!");
        GameObject* go = nullptr;
        switch (auto ty = it->get<string>(); chash(ty))
        {
        case "GameObject"_h: go = new GameObject(this); break;
        case "Mesh"_h: go = new MeshObject(this); break;
        case "LightObject"_h: go = new LightObject(this); break;
        case "Camera"_h: go = new CameraObject(this); break;
        case "Prefab"_h:
        {
            it = j.find("Path");
            if (it == j.end())
                throw runtime_error("Prefab doesn't contain a 'Path' key!");
            return LoadGameObject(fs::path(it->get<string>()));
        }
        default: throw runtime_error("Invalid GameObject type: {}"_f(ty));
        }
        assert(go != nullptr);
        go->Parent = parent;
        go->Load(j);
        AddGameObject(unique_ptr<GameObject>(go));
        return go;
    }

    GameObject* Scene::LoadGameObject(const fs::path& path, GameObject* parent)
    {
        json j;
        {
            ifstream f(path);
            if (!f.is_open())
                throw runtime_error("Failed to open file: {}"_f(path.string()));
            f >> j;
        }
        return LoadGameObject(j, parent);
    }

    void Scene::Register(GameObject* value)
    {
        auto l = lock_guard(objectMutex);
        assert(rn::none(gameObjects, value));
        auto it = rn::find(gameObjects, nullptr);
        gameObjects.emplace(it, value);
        Dispatch(value,
            [this](CameraObject* cam) { cameraObjects.emplace_back(cam); },
            [this](LightObject* light) { lightObjects.emplace_back(light); },
            [this](MeshObject* mesh) { meshObjects.emplace_back(mesh); }
        );
    }

    void Scene::UnRegister(GameObject* value)
    {
        auto l = lock_guard(objectMutex);
        assert(rn::any(gameObjects, value));
        gameObjects.erase(rn::find(gameObjects, value));
        Dispatch(value,
            [this](CameraObject* cam) { erase(cameraObjects, cam); },
            [this](LightObject* light) { erase(lightObjects, light); },
            [this](MeshObject* mesh) { erase(meshObjects, mesh); }
        );
    }

    KaeyEngine* Scene::GetEngine() const
    {
        return RenderDevice->Engine;
    }

    Engine::RenderEngine* Scene::GetRenderEngine() const
    {
        return RenderDevice->RenderEngine;
    }

    Engine::ThreadPool* Scene::GetThreadPool() const
    {
        return RenderDevice->ThreadPool;
    }

    Engine::Time* Scene::GetTime() const
    {
        return RenderDevice->Time;
    }

    GameObject::GameObject(Engine::Scene* scene) :
        scene(scene),
        parent(nullptr),
        childrenMutex(make_unique<mutex>()),
        name("{}"_f((void*)this)),
        position(0, 0, 0),
        rotation(0, 0, 0, 1),
        scale(1)
    {
        
    }

    GameObject::~GameObject() noexcept = default;

    string_view GameObject::GetName() const
    {
        return name;
    }

    void GameObject::SetName(string value)
    {
        if (value.empty())
            return;
        name = move(value);
    }

    GameObject* GameObject::GetParent() const
    {
        return parent;
    }

    void GameObject::SetParent(GameObject* value)
    {
        assert(value != this);
        if (parent == value)
            return;
        if (parent)
        {
            auto l = lock_guard(*parent->childrenMutex);
            erase(parent->children, this);
        }
        if (value)
        {
            auto l = lock_guard(*value->childrenMutex);
            value->children.emplace_back(this);
        }
        parent = value;
        OnTransformChange();
    }

    void GameObject::AddChild(GameObject* child)
    {
        child->SetParent(this);
    }

    void GameObject::SetPosition(const Vector3& value)
    {
        position = value;
        OnTransformChange();
    }

    void GameObject::SetRotation(const Quaternion& value)
    {
        rotation = value;
        eulerRotation = rotation.EulerAngle;
        rotation.Normalize();
        OnTransformChange();
    }

    void GameObject::SetScale(const Vector3& value)
    {
        scale = value;
        OnTransformChange();
    }

    const Matrix4& GameObject::GetTransformMatrix() const
    {
        if (!trMatrix)
        {
            trMatrix.emplace(Matrix4::Transformation(Position, Rotation, Scale));
            if (parent)
                trMatrix = parent->TransformMatrix * *trMatrix;
        }
        return *trMatrix;
    }

    const Matrix4& GameObject::GetNormalMatrix() const
    {
        if (!normalMatrix)
            normalMatrix.emplace(TransformMatrix.Inverse.Transposed);
        return *normalMatrix;
    }

    void GameObject::OnTransformChange()
    {
        trMatrix = nullopt;
        normalMatrix = nullopt;
        for (auto child : children)
            child->OnTransformChange();
    }

    void GameObject::Load(const json& j)
    {
        if (auto it = j.find("Type"); it != j.end() && it->is_string() && it->get<string>() == "Prefab")
        {
            json jj;
            {
                auto p = j["Path"].get<string>();
                ifstream f(p);
                if (!f.is_open())
                    throw runtime_error("Failed to find file: {}"_f(p));
                f >> jj;
            }
            return Load(jj);
        }
        if (auto it = j.find("Name"); it != j.end() && it->is_string())
        {
            Name = it->get<string>();
        }
        if (auto it = j.find("Position"); it != j.end() && it->is_array())
        {
            auto pos = it->get<vector<float>>();
            position = { pos[0], pos[1], pos[2] };
        }
        if (auto it = j.find("Rotation"); it != j.end() && it->is_array())
        {
            auto rot = it->get<vector<float>>();
            rotation = { rot[0], rot[1], rot[2], rot[3] };
        }
        if (auto it = j.find("Scale"); it != j.end() && it->is_array())
        {
            auto sca = it->get<vector<float>>();
            scale = { sca[0], sca[1], sca[2] };
        }
        if (auto it = j.find("Children"); it != j.end() && it->is_array())
        {
            vector<future<GameObject*>> childrenTasks;
            for (auto& [k, e] : it->items())
                childrenTasks.emplace_back(ThreadPool->Submit([&]
                {
                    return
                        e.is_object() ? Scene->LoadGameObject(e, this) :
                        e.is_string() ? Scene->LoadGameObject(fs::path(e.get<string>()), this) :
                        throw runtime_error("Invalid child!");
                }));
            for (auto& task : childrenTasks)
                task.get();
        }
        OnTransformChange();
    }

    void GameObject::Save(json& j) const
    {
        j["Type"] = "GameObject";
        if (Name != "{}"_f((void*)this))
            j["Name"] = Name;
        if (Position != Vector3::Zero)
            j["Position"] = Position | to_vector;
        if (Rotation != Quaternion::Identity)
            j["Rotation"] = Rotation | to_vector;
        if (Scale != Vector3::One)
            j["Scale"] = Scale | to_vector;
        auto ch = Children | vs::transform([](auto child) { return child->Save(); }) | to_vector;
        if (!ch.empty())
            j["Children"] = ch;
    }

    json Engine::GameObject::Save() const
    {
        json j;
        Save(j);
        return j;
    }

    void GameObject::OnGui()
    {
        using namespace ImGui;
        TextUnformatted(NameOf());
        TextUnformatted(name);
        auto sca = scale;
        if (DragFloat3("Position", position.raw, .1f) |
            DragFloat3("Rotation", eulerRotation.raw) |
            DragFloat3("Scale", sca.raw, .1f) |
            Checkbox("Lock Scale", &lockScale))
        {
            rotation = Quaternion::EulerAngles(eulerRotation * linm::constants<float>::Pi / 180);
            if (!lockScale)
                scale = sca;
            else for (size_t i = 0; i < 3; ++i) if (sca[i] != scale[i])
            {
                scale = Vector3::One * sca[i];
                break;
            }
            OnTransformChange();
        }
    }

    LightObject::LightObject(Engine::Scene* scene) : ParentClass(scene), lightData{ {  }, { 1, 1, 1, 1 } }
    {
        
    }

    void LightObject::OnTransformChange()
    {
        GameObject::OnTransformChange();
        lightData.Position = Position;
    }

    void LightObject::OnGui()
    {
        GameObject::OnGui();
        ImGui::ColorEdit3("Color", lightData.Color.raw);
        ImGui::DragFloat("Intensity", &lightData.Color.w, 0.05f, 0, 1000);
        lightData.Color.w = std::max(lightData.Color.w, 0.f);
    }

    void LightObject::Load(const json& j)
    {
        GameObject::Load(j);
        auto it = j.find("Color");
        if (it != j.end() && it->is_array())
        {
            auto v = it->get<vector<float>>();
            for (size_t i = 0; i < std::max<size_t>(v.size(), 3); ++i)
                lightData.Color[i] = v[i];
        }
        it = j.find("Intensity");
        if (it != j.end() && it->is_number())
            lightData.Color.w = it->get<float>();
    }

    void LightObject::Save(json& j) const
    {
        GameObject::Save(j);
        j["Type"] = "LightObject";
        j["Color"] = lightData.Color.xyz | to_vector;
        j["Intensity"] = lightData.Color.w;
    }

    CameraObject::CameraObject(Engine::Scene* scene) :
        ParentClass(scene),
        fov(60_deg), far(1000), near(.01f),
        cameraMode(CameraMode::Perspective),
        frame(make_unique<Engine::Frame>(scene->RenderDevice)),
        targetTexture(new Texture(scene->RenderDevice, Vector2{ 800, 600 })),
        targetDepthTexture(new Texture(scene->RenderDevice, Vector2{ 800, 600 }, { vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment }))
    {
        
    }

    void CameraObject::OnTransformChange()
    {
        GameObject::OnTransformChange();
        viewMatrix = nullopt;
    }

    void CameraObject::Load(const json& j)
    {
        GameObject::Load(j);
        if (auto it = j.find("Fov"); it != j.end() && it->is_number_float())
            Fov = it->get<float>();
        if (auto it = j.find("Far"); it != j.end() && it->is_number_float())
            Far = it->get<float>();
        if (auto it = j.find("Near"); it != j.end() && it->is_number_float())
            Near = it->get<float>();
        if (auto it = j.find("CameraMode"); it != j.end())
            CameraMode = it->get<enum CameraMode>();
    }

    void CameraObject::Save(json& j) const
    {
        GameObject::Save(j);
        j["Type"] = "Camera";
        j["Fov"] = Fov;
        j["Far"] = Far;
        j["Near"] = Near;
        j["CameraMode"] = CameraMode;
    }

    void CameraObject::OnGui()
    {
        GameObject::OnGui();
    }

    void CameraObject::SetFov(float value)
    {
        if (fov == value)
            return;
        fov = value;
        projectionMatrix = nullopt;
    }

    void CameraObject::SetFar(float value)
    {
        if (far == value)
            return;
        far = value;
        projectionMatrix = nullopt;
    }

    void CameraObject::SetNear(float value)
    {
        if (near == value)
            return;
        near = value;
        projectionMatrix = nullopt;
    }

    void CameraObject::SetCameraMode(Engine::CameraMode value)
    {
        if (cameraMode == value)
            return;
        cameraMode = value;
        projectionMatrix = nullopt;
    }

    Matrix4 CameraObject::GetViewMatrix() const
    {
        return viewMatrix ?
            *viewMatrix :
            viewMatrix.emplace(Matrix4::Translation(-Position) * (CameraRotationDelta * Rotation).Matrix);
    }

    Matrix4 CameraObject::GetProjectionMatrix() const
    {
        static constexpr auto fovFix = 90_deg / 600;
        auto& [w, h] = targetTexture->Extent;
        auto fix = fovFix * float(h);
        fix = std::min<float>(1, fix);
        auto fov = Fov * fix;
        return cameraMode == CameraMode::Perspective ?
            Matrix4::Perspective(fov, float(w) / float(h), Near, Far) :
            throw runtime_error("Not implemented yet!");
    }

    u32 VertexAttribute::GetTypeSize() const
    {
        switch (Type)
        {
        case AttributeType::Float: return sizeof(float);
        case AttributeType::Float2: return 2 * sizeof(float);
        case AttributeType::Float3: return 4 * sizeof(float);
        case AttributeType::Float4: return 4 * sizeof(float);
        case AttributeType::I32: return sizeof(float);
        case AttributeType::U32: return sizeof(float);
        default: throw runtime_error("Invalid AttributeType!");
        }
    }

    MeshObject::MeshObject(Engine::Scene* scene) :
        ParentClass(scene),
        meshData(nullptr),
        shapeIndex(0),
        lockShape(false),
        updateRequired(false),
        uvIndex(0)
    {

    }

    MeshObject::MeshObject(Engine::Scene* scene, shared_ptr<Engine::MeshData> meshData, vector<shared_ptr<Engine::Material>> mats) :
        ParentClass(scene),
        meshData((assert(meshData != nullptr), move(meshData))),
        materials(move(mats)),
        shapeIndex(0),
        lockShape(false),
        updateRequired(true),
        shapeValues(MeshData->ShapeCount - 1),
        uvIndex(RenderDevice->AllocateAttribute(MeshData->VertexBuffer->Count * 2))
    {
        vertexBuffer = make_unique<DefinedMemoryBuffer<Vertex>>(MeshData->RenderDevice, MeshData->VertexBuffer->Count, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
        tbnBuffer = make_unique<DefinedMemoryBuffer<TBNInfo>>(MeshData->RenderDevice, MeshData->FaceCount, vk::BufferUsageFlagBits::eStorageBuffer);
        if (MeshData->ShapeCount > 1)
        {
            shapeDeltasBuffer = make_unique<DefinedMemoryBuffer<float>>(MeshData->RenderDevice, shapeValues, vk::BufferUsageFlagBits::eStorageBuffer, false);
            shapeComputeData = RenderDevice->ShapeKeysPipeline->CreateData({ shapeDeltasBuffer.get(), MeshData->ShapeBuffer, VertexBuffer });
        }
        faceTbnData = RenderDevice->CalcFaceTBNPipeline->CreateData({ MeshData->IndexBuffer, VertexBuffer, tbnBuffer.get() });
        tbnData = RenderDevice->CalcVertexTBNPipeline->CreateData({ MeshData->TBNIndexBuffer, MeshData->FaceIndexBuffer, tbnBuffer.get(), VertexBuffer });
        u32 matIndex = 0;
        for (auto& r : MeshData->MaterialRanges)
            matIndex = std::max(matIndex, r.MaterialId);
        ++matIndex;
        materials.resize(matIndex);
        auto vertexCount = MeshData->VertexBuffer->Count;
        RenderDevice->AttributeBuffer->MapMemory([&](span<Vector4> v)
        {
            for (u32 i = 0; i < vertexCount; ++i)
                v[i].xy = MeshData->Uvs[i];
        }, { .Offset = uvIndex, .Size = vertexCount });
    }

    MeshObject::~MeshObject() = default;

    u32 MeshObject::GetShapeIndex() const
    {
        return shapeIndex;
    }

    void MeshObject::SetShapeIndex(u32 value)
    {
        if (value == shapeIndex)
            return;
        shapeIndex = value;
        updateRequired = true;
    }

    bool MeshObject::GetLockShape() const
    {
        return lockShape;
    }

    void MeshObject::SetLockShape(bool value)
    {
        if (lockShape == value)
            return;
        lockShape = value;
        updateRequired = true;
    }

    float MeshObject::GetShapeValues(u32 i) const
    {
        return shapeValues[i];
    }

    void MeshObject::SetShapeValues(u32 i, float value)
    {
        if (shapeValues[i] == value)
            return;
        shapeValues[i] = value;
        updateRequired = true;
    }

    void MeshObject::OnTransformChange()
    {
        GameObject::OnTransformChange();
    }

    void MeshObject::Load(const json& j)
    {
        auto it = j.find("Path");
        if (it == j.end())
            throw runtime_error("MeshObject doesn't contain a 'Path' key!");

        auto path = (fs::path)it->get<string>();
        auto md = Project ?
            Project->FindOrCreateMeshData(path, RenderDevice, path) :
            make_shared<Engine::MeshData>(RenderDevice, move(path));
        auto matPaths = md->Materials | vs::transform([](fs::path p) { return p; }) | to_vector;

        if (it = j.find("Materials"); it != j.end() && it->is_array())
        {
            auto items = it->get<vector<json>>();
            auto size = std::min(matPaths.size(), items.size());
            for (size_t i = 0; i < size; ++i)
            {
                auto& jj = items[i];
                if (jj.is_string())
                    matPaths[i] = jj.get<string>();
                else matPaths[i] = "Materials" / matPaths[i];
            }
        }
        else
        {
            for (auto& matPath : matPaths)
            {
                matPath = "Materials" / matPath;
                matPath.replace_extension(".json");
            }
        }

        {
            auto obj = MeshObject(Scene, move(md), vector<shared_ptr<Engine::Material>>(matPaths.size()));
            swap(meshData, obj.meshData);
            swap(vertexBuffer, obj.vertexBuffer);
            swap(shapeDeltasBuffer, obj.shapeDeltasBuffer);
            swap(tbnBuffer, obj.tbnBuffer);
            swap(vertexAttributeBuffer, obj.vertexAttributeBuffer);
            swap(materials, obj.materials);
            swap(shapeValues, obj.shapeValues);
            swap(shapeComputeData, obj.shapeComputeData);
            swap(faceTbnData, obj.faceTbnData);
            swap(tbnData, obj.tbnData);
            swap(uvIndex, obj.uvIndex);
            updateRequired = true;
        }

        auto gp = RenderDevice->DiffusePipeline;
        ThreadPool->ParallelSubmit(matPaths.size(), [=, this](size_t i)
        {
            Materials[i] = Project->FindOrCreateMaterial(matPaths[i], Scene->Project, gp, matPaths[i]);
        });

        if (it = j.find("LockShape"); it != j.end() && it->is_boolean())
            LockShape = it->get<bool>();

        if (it = j.find("ShapeIndex"); it != j.end() && it->is_number_integer())
            ShapeIndex = it->get<u32>();

        if (it = j.find("Shape Values"); it != j.end())
        {
            auto shapeMap = it->get<unordered_map<string, float>>();
            for (auto& [name, value] : shapeMap)
            for (u32 i = 0; i < (u32)MeshData->ShapeNames.size(); ++i) if (MeshData->ShapeNames[i] == name)
                ShapeValues[i - 1] = value;
        }
        
        GameObject::Load(j);
    }

    void MeshObject::Save(json& j) const
    {
        GameObject::Save(j);
        j["Type"] = "Mesh";
        j["Path"] = relative(Project->PathOf(MeshData.get()));
        if (!modifiers.empty())
            j["Modifiers"] = modifiers | vs::transform([](auto& mod)
            {
                json jj;
                mod->Save(jj);
                return jj;
            }) | to_vector;
        if (LockShape)
            j["LockShape"] = true;
        if (ShapeIndex != 0)
            j["ShapeIndex"] = ShapeIndex;
        if (MeshData->ShapeCount > 1)
        {
            unordered_map<string, float> shapeMap;
            for (u32 i = 0; i + 1 < MeshData->ShapeCount; ++i) if (ShapeValues[i] != 0)
                shapeMap.emplace(MeshData->ShapeNames[i + 1], ShapeValues[i]);
            if (!shapeMap.empty())
                j["Shape Values"] = shapeMap;
        }
        
        if (!materials.empty())
        {
            vector<fs::path> matPaths;
            for (auto& mat : materials)
                matPaths.emplace_back(relative(Project->PathOf(mat.get())));
            j["Materials"] = matPaths;
        }

    }

    void MeshObject::OnGui()
    {
        using namespace ImGui;
        MeshModifier* toRemove = nullptr;

        GameObject::OnGui();
        if (Button("Force Update"))
            updateRequired = true;
        Spacing();
        auto meshDatas = Project->Meshes;
        auto names = meshDatas | vs::transform([this](Engine::MeshData* md) { return Project->NameOf(md).data(); }) | to_vector;
        auto it = rn::find(meshDatas, MeshData.get());
        auto index = i32(it != meshDatas.end() ? it - meshDatas.begin() : -1);
        PushID(&meshData);
        if (Combo("", &index, names.data(), int(names.size())))
            Engine->SubmitSyncronized([this, md = Project->SharedOf(meshDatas[index])] { MeshData = md; });
        PopID();

        SliderFloat("Metallic", &metallic, 0, 1);
        SliderFloat("Roughness", &roughness, 0, 1);

        Spacing();
        if (TreeNode("Info"))
        {
            TextUnformatted("Vertex Size: {}"_f(sizeof Vertex));
            TextUnformatted("Vertex Count: {}"_f(MeshData->VertexBuffer->Count));
            TextUnformatted("Index Count: {}"_f(MeshData->IndexBuffer->Count));
            TreePop();
        }
        Spacing();
        if (TreeNode("Attributes"))
        {
            Text("Vertex Attributes");
            for (auto& att : vertexAttributes)
                Text("%s: %s -> %i", att.Name.data(), magic_enum::enum_name(att.Type).data(), att.Offset);
            Separator();
            InputText("Name", &attName);
            Combo("Type", &attributeType);
            if (Button("Add Attribute") && !attName.empty())
                AddVertexAttribute(attributeType, move(attName));
            TreePop();
        }
        Spacing();
        if (auto shapeCount = MeshData->ShapeCount; shapeCount > 1 && TreeNode("Shapes"))
        {
            if (auto lock = LockShape; Checkbox("Lock Shape", &lock))
                LockShape = lock;
            if (LockShape)
            {
                auto idx = ShapeIndex;
                string_view n = MeshData->ShapeNames[idx];
                if (SliderInt("Shape", (int*)&idx, 0, (int)shapeCount, n.data()))
                    ShapeIndex = idx;
            }
            else for (u32 i = 0; i + 1 < shapeCount; ++i)
                if (auto v = ShapeValues[i]; SliderFloat(MeshData->ShapeNames[i + 1].data(), &v, 0, 1))
                    ShapeValues[i] = v;
            TreePop();
        }
        Spacing();
        if (TreeNode("Modifiers"))
        {
            PushID(&modifiers);
            Combo("", &modIndex, +[](void*, int i, const char** out) -> bool { return *out = ModifierFactories[i].first.data(); }, nullptr, (int)ModifierFactories.size());
            PopID();
            SameLine();
            if (Button("Add Modifier"))
                AddModifier(ModifierFactories[modIndex].second(MeshData->RenderDevice));
            for (auto& mod : modifiers)
            {
                Separator();
                Spacing();
                PushID(&mod);
                if (TreeNode(mod->ModifierName()))
                {
                    mod->OnGui();
                    TreePop();
                }
                PopID();
                PushID((i8*)&mod + 1);
                if (Button("Delete"))
                    toRemove = mod.get();
                PopID();
            }
            TreePop();
            
        }
        Separator();
        Spacing();
        auto mats = Project->Materials | not_null | to_vector;
        auto matNames = mats | vs::transform([this](auto m) { return Project->NameOf(m).data(); }) | to_vector;
        unordered_map<Material*, i32> matIdMap;
        matIdMap.emplace(nullptr, -1);
        for (auto i = 0; auto m : mats)
            matIdMap.emplace(m, i++);
        if (TreeNode("Materials"))
        {
            for (size_t i = 0; i < materials.size(); ++i)
            {
                auto& mat = materials[i];
                MaterialEdit(mat.get());
            }
            TreePop();
        }
        if (TreeNode("Material Ranges"))
        {
            auto idx = 0;
            for (auto& [i, first, count] : meshData->MaterialRanges) if (TreeNode("{}"_f(++idx)))
            {
                auto& mat = materials[i];
                PushID(&i);
                auto idx = matIdMap.at(mat.get());
                if (Combo("", &idx, matNames.data(), i32(matNames.size())))
                    mat = Project->SharedOf(mats[idx]);
                PopID();
                if (BeginDragDropTarget())
                if (auto payload = AcceptDragDropPayload("ITEM_PATH"))
                {
                    auto path = fs::path((const char*)payload->Data);
                    switch (chash(path.extension().string()))
                    {
                    case ".json"_h:
                    {
                        auto f = ifstream(path);
                        if (!f.is_open())
                            break;
                        json j;
                        f >> j;
                        f.close();
                        auto it = j.find("Type");
                        if (it != j.end() && it->is_string()) switch (chash(it->get<string>()))
                        {
                        case "diffuse"_h:
                        {
                            ThreadPool->Submit([&, path, j, i]
                            {
                                auto ptr = Project->FindOrCreateMaterial(path, Project, RenderDevice->DiffusePipeline);
                                ptr->Load(j);
                                Engine->SubmitSyncronized([&, i, ptr] { materials[i] = ptr; });
                            });
                        }break;
                        }
                    }break;
                    }
                    EndDragDropTarget();
                }

                PushID(&first);
                MaterialEdit(mat.get());
                PopID();
                TreePop();
            }
            TreePop();
        }
        if (toRemove != nullptr)
            RemoveModifier(toRemove);
    }

    void MeshObject::Update()
    {
        if (!updateRequired)
            return;
        
        MemoryBuffer::Copy(vertexBuffer.get(), meshData->VertexBuffer);
        if (shapeComputeData)
        {
            auto vertexCount = (u32)meshData->VertexBuffer->Count;
            if (lockShape)
            {
                if (shapeIndex > 0)
                {
                    auto vals = vector<float>(shapeValues.size(), 0);
                    vals[shapeIndex - 1] = 1;
                    shapeDeltasBuffer->WriteData(vals);
                    RenderDevice->ShapeKeysPipeline->Compute(shapeComputeData.get(), vertexCount, vertexCount, (u32)meshData->ShapeCount);
                }
            }
            else
            {
                shapeDeltasBuffer->WriteData(shapeValues);
                RenderDevice->ShapeKeysPipeline->Compute(shapeComputeData.get(), vertexCount, vertexCount, (u32)meshData->ShapeCount);
            }
        }
        for (auto& mod : modifiers)
            mod->OnUpdate();

        UpdateTBN();

        for (auto dep : dependents)
            dep->OnUpdate();
        updateRequired = false;
    }

    void MeshObject::UpdateTBN()
    {
        auto vertexCount = (u32)VertexBuffer->Count;
        RenderDevice->CalcFaceTBNPipeline->Compute(faceTbnData.get(), (u32)meshData->FaceCount, (u32)meshData->FaceCount);
        RenderDevice->CalcVertexTBNPipeline->Compute(tbnData.get(), vertexCount, vertexCount, uvIndex + vertexCount);
    }

    void MeshObject::AddModifier(unique_ptr<MeshModifier> modifier)
    {
        modifier->OnAdd(this);
        modifiers.emplace_back(move(modifier));
    }

    unique_ptr<MeshModifier> MeshObject::RemoveModifier(MeshModifier* modifier)
    {
        auto it = rn::find_if(modifiers, [=](auto& ptr) { return ptr.get() == modifier; });
        if (it == modifiers.end())
            return nullptr;
        auto ptr = move(*it);
        modifiers.erase(it);
        ptr->OnRemove();
        return ptr;
    }

    void MeshObject::AddDependent(MeshModifier* mod)
    {
        if (auto it = rn::find(dependents, mod); it == dependents.end())
            dependents.emplace_back(mod);
    }

    void MeshObject::RemoveDependent(MeshModifier* mod)
    {
        if (auto it = rn::find(dependents, mod); it != dependents.end())
            dependents.erase(it);
    }

    const VertexAttribute& Engine::MeshObject::AddVertexAttribute(AttributeType type, string name)
    {
        auto size = vertexAttributeBuffer ? vertexAttributeBuffer->Size : 0;
        auto& att = vertexAttributes.emplace_back(type, move(name), size);
        size += att.TypeSize * MeshData->VertexBuffer->Count;
        auto ptr = Scene->RenderDevice->AllocateMemory<void>(size, vk::BufferUsageFlagBits::eStorageBuffer);
        if (vertexAttributeBuffer)
            MemoryBuffer::Copy(ptr.get(), vertexAttributeBuffer.get());
        vertexAttributeBuffer = move(ptr);
        return att;
    }

    void Engine::MeshObject::SetMeshData(const shared_ptr<Engine::MeshData>& value)
    {
        if (meshData == value)
            return;
        auto obj = MeshObject(Scene, move(value), {});
        swap(meshData, obj.meshData);
        swap(vertexBuffer, obj.vertexBuffer);
        swap(shapeDeltasBuffer, obj.shapeDeltasBuffer);
        swap(tbnBuffer, obj.tbnBuffer);
        swap(vertexAttributeBuffer, obj.vertexAttributeBuffer);
        swap(materials, obj.materials);
        swap(shapeComputeData, obj.shapeComputeData);
        swap(faceTbnData, obj.faceTbnData);
        swap(tbnData, obj.tbnData);
        updateRequired = true;
    }

    SurfaceDeformModifier::SurfaceDeformModifier(RenderDevice* device) :
        device(device),
        mesh(nullptr),
        target(nullptr),
        status(BindingStatus::UnBound)
    {
        
    }

    void SurfaceDeformModifier::SetTarget(MeshObject* value)
    {
        if (target == value || value == mesh)
            return;
        UnBind();
        target = value;
    }

    void SurfaceDeformModifier::OnAdd(MeshObject* mesh)
    {
        this->mesh = mesh;
    }

    void SurfaceDeformModifier::OnUpdate()
    {
        if (status != BindingStatus::Bound)
            return;
        auto vertexCount = (u32)mesh->MeshData->VertexBuffer->Count;
        device->SurfaceDeformPipeline->Compute(deformData.get(), vertexCount, vertexCount);
    }

    void SurfaceDeformModifier::OnRemove()
    {
        mesh = nullptr;
    }

    void SurfaceDeformModifier::OnGui()
    {
        using namespace ImGui;

        BeginDisabled(Status != BindingStatus::UnBound);
        if (DragFloat("Max Distance", &maxDistance, 0, 1))
            mesh->updateRequired = true;
        auto meshes = mesh->Scene->MeshObjects | vs::filter([&](auto ptr) { return ptr != mesh; }) | to_vector;
        meshes.insert(meshes.begin(), nullptr);
        PushID(&target);
        Combo("Target", &targetIdx, +[](void* ptr, int i, const char** out) -> bool
        {
            auto v = (MeshObject**)ptr;
            return *out = v[i] ? v[i]->Name.data() : "";
        }, meshes.data(), (int)meshes.size());
        PopID();
        EndDisabled();

        PushID(this);
        BeginDisabled(targetIdx == 0 || Status == BindingStatus::Binding);
        if (auto check = Status == BindingStatus::Bound; Button(check ? "UnBind" : "Bind"))
        {
            SetTarget(meshes[targetIdx]);
            if (check)
                UnBind();
            else Bind();
        }
        EndDisabled();
        PopID();
    }

    string_view SurfaceDeformModifier::ModifierName()
    {
        return "Surface Deform";
    }

    void SurfaceDeformModifier::Load(const json& j)
    {
        
    }

    void SurfaceDeformModifier::Save(json& j)
    {
        
    }

    void SurfaceDeformModifier::Bind()
    {
        if (!target || status != BindingStatus::UnBound)
            return;
        status = BindingStatus::Binding;
        device->ThreadPool->Submit([&]
        {
            auto vertexCount = (u32)mesh->VertexBuffer->Count;
            bindBuffer = device->AllocateMemory<VertexBinding>(vertexCount, vk::BufferUsageFlagBits::eStorageBuffer);
            auto bindData = device->BindPipeline->CreateData({ mesh->VertexBuffer, target->VertexBuffer, bindBuffer.get() });
            device->BindPipeline->Compute(bindData.get(), vertexCount, vertexCount, (u32)target->VertexBuffer->Count, maxDistance);
            auto v = bindBuffer->ReadData();
            deformData = device->SurfaceDeformPipeline->CreateData({ target->VertexBuffer, bindBuffer.get(), mesh->VertexBuffer });
            target->AddDependent(this);
            status = BindingStatus::Bound;
        });
    }

    void SurfaceDeformModifier::UnBind()
    {
        mesh->updateRequired = true;
        if (status == BindingStatus::UnBound)
            return;
        target->RemoveDependent(this);
        bindBuffer = nullptr;
        deformData = nullptr;
        status = BindingStatus::UnBound;
    }

    DisplaceModifier::DisplaceModifier(Engine::RenderDevice* renderDevice) :
        renderDevice(renderDevice), mesh(nullptr),
        value(0)
    {
        
    }

    void DisplaceModifier::OnAdd(MeshObject* mesh)
    {
        this->mesh = mesh;
        displaceData = renderDevice->DisplacePipeline->CreateData({ mesh->VertexBuffer });
    }

    void DisplaceModifier::OnUpdate()
    {
        mesh->UpdateTBN();
        auto vertexCount = mesh->VertexBuffer->Count;
        renderDevice->DisplacePipeline->Compute(displaceData.get(), vertexCount, vertexCount, value);
    }

    void DisplaceModifier::OnRemove()
    {
        mesh->updateRequired = value != 0;
        mesh = nullptr;
        displaceData = nullptr;
    }

    void DisplaceModifier::OnGui()
    {
        using namespace ImGui;
        if (DragFloat("Displace Value", &value, 0.05f))
            mesh->updateRequired = true;
    }

    string_view DisplaceModifier::ModifierName()
    {
        return "Displace";
    }

    void DisplaceModifier::Load(const json& j)
    {
        
    }

    void DisplaceModifier::Save(json& j)
    {
        
    }

    float DisplaceModifier::GetValue() const
    {
        return value;
    }

    void DisplaceModifier::SetValue(float value)
    {
        if (this->value == value)
            return;
        this->value = value;
        mesh->updateRequired = true;
    }

}
