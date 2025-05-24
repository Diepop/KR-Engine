#include "Kaey/Renderer/ImGui.hpp"
#include "Kaey/Renderer/Renderer.hpp"
#include "Kaey/Renderer/Scene3D.hpp"
#include "Kaey/Renderer/ThreadPool.hpp"
#include "Kaey/Renderer/Time.hpp"
#include "Kaey/Renderer/Window.hpp"

#include <RenderTexPipeline.hpp>
#include <EnvBRDFPipeline.hpp>

#include "Kaey/Renderer/Utility.hpp"

using namespace Kaey::Renderer;
using namespace Kaey;

extern const auto Assets  = fs::path(ASSETS_PATH);
extern const auto Shaders = fs::path(SHADERS_PATH);

struct GltfTransform
{
    Vector3    Position;
    Quaternion Rotation;
    Vector3    Scale;
    Matrix4    Transform;

    GltfTransform(Vector3 position = 0_xyz, Quaternion rotation = Quaternion::Identity, Vector3 scale = 1_xyz, Matrix4 transform = Matrix4::Identity) :
        Position(position),
        Rotation(rotation),
        Scale(scale),
        Transform(transform)
    {

    }

    GltfTransform(const tinygltf::Node& node)
    {
        cspan<double> v;
        Position  = (v = node.translation).empty() ? 0_xyz                : Vector3   {-f32(v[0]), f32(v[1]), f32(v[2]) };
        Rotation  = (v = node.rotation   ).empty() ? Quaternion::Identity : Quaternion{-f32(v[0]), f32(v[1]), f32(v[2]), f32(v[3]) };
        Scale     = (v = node.scale      ).empty() ? 1_xyz                : Vector3   { f32(v[0]), f32(v[1]), f32(v[2]) };
        Transform = Matrix4::Transformation(Position, Rotation, Scale);
    }

};

template<class T>
struct GltfBufferView
{
    struct Iterator
    {
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = ptrdiff_t;
        using pointer           = const T*;
        using reference         = const T&;

        Iterator(const GltfBufferView* ptr = nullptr, ptrdiff_t index = 0) : ptr(ptr), index(index) {  }

        reference operator*() const { return *(pointer)((u8*)ptr->Pointer + ptr->ByteStride * index); }
        pointer operator->() const { return **this; }

        reference operator[](auto i) const { return *(*this + i); }

        friend auto operator<=>(const Iterator& lhs, const Iterator& rhs) { return lhs.index <=> rhs.index; }
        friend auto operator==(const Iterator& lhs, const Iterator& rhs) { return lhs.index == rhs.index; }

        friend Iterator& operator+=(Iterator& lhs, auto rhs) { return lhs.index += rhs, lhs; }
        friend Iterator& operator-=(Iterator& lhs, auto rhs) { return lhs.index -= rhs, lhs; }

        friend Iterator operator+(Iterator lhs, auto rhs) { return lhs += rhs; }
        friend Iterator operator-(Iterator lhs, auto rhs) { return lhs -= rhs; }
        friend Iterator operator+(auto lhs, Iterator rhs) { return rhs += lhs; }
        friend Iterator operator-(auto lhs, Iterator rhs) { return rhs -= lhs; }

        friend ptrdiff_t operator-(const Iterator& lhs, const Iterator& rhs) { return lhs.index - rhs.index; }

        Iterator& operator++() { return *this += 1, *this; }
        Iterator& operator--() { return *this -= 1, *this; }

        Iterator operator++(int) { return { ptr, index++ }; }
        Iterator operator--(int) { return { ptr, index-- }; }

    private:
        const GltfBufferView* ptr;
        ptrdiff_t index;
    };

    const T* Pointer;
    size_t Count;
    size_t ByteStride;
    int ComponentType;

    GltfBufferView(const T* pointer, size_t count, size_t byteStride, int componentType) : Pointer(pointer), Count(count), ByteStride(byteStride), ComponentType(componentType)
    {

    }

    GltfBufferView(const tinygltf::Model* model, const tinygltf::Accessor& accessor)
    {
        Count = accessor.count;
        if (Count > 0)
        {
            auto& bv = model->bufferViews[accessor.bufferView];
            auto& buf = model->buffers[bv.buffer];
            Pointer = reinterpret_cast<const T*>(buf.data.data() + bv.byteOffset + accessor.byteOffset);
            ByteStride = accessor.ByteStride(bv);
            ComponentType = accessor.componentType;
        }
        else
        {
            Pointer = nullptr;
            ByteStride = 1;
            ComponentType = -1;
        }
    }

    GltfBufferView(const tinygltf::Model* model, int accessorIndex) : GltfBufferView(model, model->accessors[accessorIndex])
    {

    }

    GltfBufferView(const tinygltf::Model* model, int bufferView, size_t count) : GltfBufferView(model, [=]
    {
        tinygltf::Accessor accessor;
        accessor.bufferView = bufferView;
        accessor.count = count;
        return accessor;
    }())
    {

    }

    size_t size() const { return Count; }

    decltype(auto) operator[](this auto& self, auto i) { return self.begin()[i]; }

    Iterator begin() const { return { this, 0 }; }
    Iterator end() const { return { this, (ptrdiff_t)Count }; }

    template<class To>
    auto& As() const { return reinterpret_cast<const GltfBufferView<To>&>(*this); }

    template<class To>
    auto& As() { return reinterpret_cast<GltfBufferView<To>&>(*this); }

};

namespace
{
    [[maybe_unused]]
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
            auto [albmTex, albmTask] = Texture::LoadSharedAsync(threadPool, scene->Device, Assets / "Textures/{}/albm.png"_f(name), { .Format = vk::Format::eR8G8B8A8Srgb,  .MaxMipLevel = 0, .ClearColor = 1_xyz + 0_w });
            auto [nrmsrTex, nrmsrTask] = Texture::LoadSharedAsync(threadPool, scene->Device, Assets / "Textures/{}/nrmsr.png"_f(name), { .Format = vk::Format::eR8G8B8A8Unorm, .MaxMipLevel = 0, .ClearColor = .5_xyzw });
            auto [paTex, paTask] = Texture::LoadSharedAsync(threadPool, scene->Device, Assets / "Textures/{}/pa.png"_f(name), { .Format = vk::Format::eR8G8B8A8Unorm, .MaxMipLevel = 0, .ClearColor = 1_xyzw });
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

        auto monkey = MeshInstanceId(0);
        auto sqrBall = MeshInstanceId(1);
        auto ball = MeshInstanceId(2);
        auto box = MeshInstanceId(3);

        scene->SetMeshMaterial(box, 0, boxMat);
        scene->SetMeshMaterial(monkey, 0, metalMat);
        scene->SetMeshMaterial(sqrBall, 0, brickMat);
        scene->SetMeshMaterial(ball, 0, carbonMat);
    }

    void LoadScene(Scene3D* scene, const tinygltf::Model& model)
    {
        if (model.defaultScene == -1)
            return;
        auto root = model.scenes[model.defaultScene];

        auto writer = BufferQueue(scene->Device);
        auto textureMap = unordered_map<int, TextureId>{ { -1, nullid } };
        auto loadTexture = [&](int texId, bool nonColor) -> TextureId
        {
            auto [it, check] = textureMap.emplace(texId, nullid);
            if (!check)
                return it->second;
            auto& tex = model.textures[texId];
            auto& img = model.images[tex.source];
            auto shared = make_shared<Texture>(scene->Device, TextureArgs{ .Size = { (u32)img.width, (u32)img.height }, .Format = nonColor ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb, .MaxMipLevel = 0 });
            writer.QueueWrite(shared.get(), img.image);
            return it->second = scene->AddTexture(move(shared));
        };

        auto materialMap = unordered_map<int, MaterialId>{ { -1, nullid } };
        auto loadMaterial = [&](int matId) -> MaterialId
        {
            auto [it, check] = materialMap.emplace(matId, nullid);
            if (!check)
                return it->second;
            auto& material = model.materials[matId];
            auto& pbr = material.pbrMetallicRoughness;
            auto& id = it->second = scene->CreateMaterial();
            scene->SetMaterialAlbedoMetallicTexture(id, loadTexture(pbr.baseColorTexture.index, false));
            scene->SetMaterialNormalSpecularRoughness(id, loadTexture(material.normalTexture.index, true));
            scene->SetMaterialAlbedoMultiplier(id, { (f32)pbr.baseColorFactor[0], (f32)pbr.baseColorFactor[1], (f32)pbr.baseColorFactor[2] });
            scene->SetMaterialMetallicMultiplier(id, (f32)pbr.metallicFactor);
            return id;
        };

        auto nameMap = unordered_map<string_view, int>{  };
        auto transformMap = unordered_map<int, GltfTransform>{ { -1, {} } };
        rn::for_each(root.nodes, [&](this auto& self, int nodeId) -> void
        {
            auto& node = model.nodes[nodeId];
            nameMap.emplace(node.name, nodeId);
            transformMap.emplace(nodeId, GltfTransform(node));
            rn::for_each(node.children, self);
        });

        auto skeletonMap = unordered_map<int, SkeletonId>{ { -1, nullid } };
        auto boneMap = unordered_map<int, BoneId>{ { -1, nullid } };
        auto loadSkin = [&](int skinId) -> SkeletonId
        {
            if (skinId == -1)
                return nullid;
            auto& skin = model.skins[skinId];
            auto skinNodeId = nameMap.at(skin.name);
            auto [i1, check] = skeletonMap.emplace(skinNodeId, nullid);
            if (!check)
                return i1->second;

            boneMap.reserve(boneMap.size() + skin.joints.size());

            auto parentJoints = skin.joints | to_set;
            rn::for_each(skin.joints, [&](this auto& self, int jointId) -> void
            {
                auto& joint = model.nodes[jointId];
                for (auto childId : joint.children)
                {
                    parentJoints.erase(childId);
                    self(childId);
                }
            });

            auto skId = i1->second = scene->CreateSkeleton();

            rn::for_each(parentJoints, [&](this auto& self, int jointId, BoneId parentId = nullid, Matrix4 mat = Matrix4::Identity) -> void
            {
                auto boneId = scene->CreateBone(skId, parentId);
                boneMap.emplace(jointId, boneId);
                auto& tr = transformMap.at(jointId);
                mat = tr.Transform * mat;
                scene->SetBoneRestPosition(boneId, mat[3].xyz);
                for (auto childId : model.nodes[jointId].children)
                    self(childId, boneId, mat);
            });

            return skId;
        };

        auto meshMap = unordered_map<int, MeshDataId>{ { -1, nullid } };
        auto loadMeshData = [&](int meshId, int skinId) -> MeshDataId
        {
            if (meshId == -1)
                return nullid;
            auto [it, check] = meshMap.emplace(meshId, nullid);
            if (!check)
                return it->second;

            auto vec3 = [](const Vector3& v) -> Vector3 { return { -v.x, v.y, v.z }; };

            auto& mesh = model.meshes[meshId];

            size_t indexCount = 0;
            size_t vertexCount = 0;
            size_t shapeCount = mesh.weights.size();

            vector<MeshPrimitive> primitives;
            primitives.reserve(mesh.primitives.size());
            for (auto& primitive : mesh.primitives)
            {
                auto iCount = model.accessors[primitive.indices].count;
                auto vCount = model.accessors[primitive.attributes.at("POSITION")].count;
                auto matId = loadMaterial(primitive.material);
                primitives.emplace_back(indexCount, iCount, matId);
                indexCount += iCount;
                vertexCount += vCount;
            }

            MeshWriteData vertices;
            auto& inds = vertices.Indices;
            inds.reserve(indexCount);
            vertices.Reserve(vertexCount, shapeCount);
            if (skinId != -1)
            {
                vertices.BoneIndices.reserve(vertexCount);
                vertices.BoneWeights.reserve(vertexCount);
            }

            for (auto [pId, primitive] : mesh.primitives | uindexed32)
            {
                auto indices = GltfBufferView<u16>(&model, primitive.indices);
                switch (indices.ComponentType)
                {
                case TINYGLTF_COMPONENT_TYPE_SHORT:
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    for (auto i : indices)
                        inds.emplace_back(u32(vertices.Points.size() + i));
                    break;
                case TINYGLTF_COMPONENT_TYPE_INT:
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                {
                    for (auto i : indices.As<u32>())
                        inds.emplace_back(u32(vertices.Points.size() + i));
                }break;
                default: throw runtime_error("Invalid argument 'indices.componentType'!");
                }

                auto positions = GltfBufferView<Vector3>(&model, primitive.attributes.at("POSITION"));
                auto normals = GltfBufferView<Vector3>(&model, primitive.attributes.at("NORMAL"));
                auto uvs = GltfBufferView<Vector2>(&model, primitive.attributes.at("TEXCOORD_0"));

                for (auto i : irange(positions.Count))
                {
                    vertices.Points.emplace_back(vec3(positions[i]) + 1_w);
                    vertices.Normals.emplace_back(vec3(normals[i]).Normalized + 0_w);
                    vertices.Uvs.emplace_back(uvs[i]);
                }

                if (skinId != -1)
                {
                    auto& skin = model.skins[skinId];
                    auto fillWeights = [&](auto boneIndices)
                    {
                        auto boneWeights = GltfBufferView<Vector4>(&model, primitive.attributes.at("WEIGHTS_0"));
                        assert(boneWeights.ComponentType == TINYGLTF_COMPONENT_TYPE_FLOAT);
                        for (auto i : irange(positions.Count))
                        {
                            auto& w = vertices.BoneWeights.emplace_back(boneWeights[i]);
                            for (auto [j, boneIndex] : vertices.BoneIndices.emplace_back() | indexed) if (w[j] > 0)
                                boneIndex = boneIndices[i][j];
                            //boneIndex = boneMap.at(skin.joints[boneIndices[i][j]]);
                        }
                    };
                    auto boneIndices = GltfBufferView<array<u8, 4>>(&model, primitive.attributes.at("JOINTS_0"));
                    switch (boneIndices.ComponentType)
                    {
                    case TINYGLTF_COMPONENT_TYPE_BYTE:
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  fillWeights(boneIndices); break;
                    case TINYGLTF_COMPONENT_TYPE_SHORT:
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: fillWeights(boneIndices.As<array<u16, 4>>()); break;
                    case TINYGLTF_COMPONENT_TYPE_INT:
                    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   fillWeights(boneIndices.As<array<u32, 4>>()); break;
                    default: throw runtime_error("Invalid bone indices component type!");
                    }
                }
            }

            if (shapeCount > 0)
            {
                auto& dpoints = vertices.DeltaPoints;
                auto& dnormals = vertices.DeltaNormals;

                dpoints.insert_range(dpoints.end(), vertices.Points);
                dnormals.insert_range(dnormals.end(), vertices.Normals);

                for (auto shapeIndex : irange(shapeCount))
                for (auto& primitive : mesh.primitives)
                {
                    auto& target = primitive.targets[shapeIndex];

                    auto spoints = GltfBufferView<Vector3>(&model, target.at("POSITION"));
                    auto snormals = GltfBufferView<Vector3>(&model, target.at("NORMAL"));

                    for (auto i : irange(spoints.Count))
                    {
                        dpoints.emplace_back(vec3(spoints[i]) + 1_w);
                        dnormals.emplace_back(vec3(snormals[i]) + 0_w);
                    }
                }
            }

            vertices.CalcVertexTangs();

            it->second = scene->CreateMeshData(indexCount, vertexCount, shapeCount, primitives);

            scene->WriteMesh(it->second, vertices);

            return it->second;
        };

        auto loadMesh = [&](int nodeId) -> MeshInstanceId
        {
            auto& node = model.nodes[nodeId];
            auto mdId = loadMeshData(node.mesh, node.skin);
            if (mdId == nullid)
                return nullid;
            auto meshId = scene->CreateMeshInstance(mdId);
            auto& mi = scene->MeshInstances[meshId];
            auto& tr = transformMap.at(nodeId);
            mi.Transform->Position = tr.Position;
            mi.Transform->Rotation = tr.Rotation;
            mi.Transform->Scale = tr.Scale;
            return meshId;
        };

        rn::for_each(root.nodes, [&](this auto& self, int nodeId) -> void
        {
            auto& node = model.nodes[nodeId];
            auto skinId = loadSkin(node.skin);
            auto meshId = (MeshInstanceId)loadMesh(nodeId);
            if (meshId != nullid && skinId != nullid)
                scene->SetMeshSkeleton(meshId, skinId);
            rn::for_each(node.children, self);
        });

        scene->Device->ExecuteSingleTimeCommands([&](Frame* fr)
        {
            writer.Execute(fr);
            fr->CommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, {});
            for (auto tex : textureMap | vs::values) if (tex != nullid)
                scene->Textures[tex]->GenerateMipmaps(fr);
        });
    }

    void LoadGltf(Scene3D* scene, crpath path, bool isBinary)
    {
        tinygltf::TinyGLTF loader;
        tinygltf::Model model;
        string err, warn;
        auto ext = path.extension();
        auto res = isBinary ? loader.LoadBinaryFromFile(&model, &err, &warn, path.string()) : loader.LoadASCIIFromFile(&model, &err, &warn, path.string());
        return res ? LoadScene(scene, model) : throw runtime_error(err);
    }

    void LoadGltf(Scene3D* scene, crpath path)
    {
        auto ext = path.extension();
        return LoadGltf(scene, path,
            ext == ".glb" ? true :
            ext == ".gltf" ? false :
            throw runtime_error("Invalid file extension, expected gltf or glb, was {}"_f(ext.string()))
        );
    }

}

int main(int argc, char* argv[])
{
    //try
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

        current_path(Shaders / "Glsl/");
        auto scene = make_unique<Scene3D>(device);

        auto [exrTex, exrTask] = Texture::LoadExrUniqueAsync(&threadPool, device, Assets / "Textures/HDRIs/courtyard_4k.exr");
        auto exrId = scene->AddTexture(exrTex.get());
        scene->EnvironmentMultiplier = 0.15_xyz + 1_w;

        //scene->AmbientLight = 0.15_xyz + 1_w;

        LoadGltf(scene.get(), Assets / "Monkey.glb");
        LoadSceneMaterials(scene.get(), &threadPool);

        //LoadGltf(scene.get(), Assets / "glTF-Sample-Models/2.0/Sponza/glTF/Sponza.gltf");
        //LoadGltf(scene.get(), Assets / "glTF-Sample-Models/2.0/ABeautifulGame/glTF/ABeautifulGame.gltf");

        auto camId       = scene->CreateCamera();
        auto cam         = &scene->Cameras[camId];
        auto cameraAngle = Vector2(180_deg, 0_deg);
        auto camRot      = [&]{ return Quaternion::AngleAxis(cameraAngle.y, 1_right) * Quaternion::AngleAxis(cameraAngle.x, 1_up); };

        //cam->Transform->Position = { 0, 1.75f, 5 };
        cam->Transform->Position = 1.75_up;
        cam->Transform->Rotation = camRot();
        scene->SetCameraScreenSize(camId, (1920_x + 1080_y) * 1.0f);
        //scene->SetCameraFov(camId, 106_deg);
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
                auto v = Matrix3::Rotation(0, 0, -lightRot) * (8_up + Matrix3::Rotation(0, f32(i) * deltaAng + lightRotation * 45_deg, 0) * 4_right);
                scene->SetLightPosition(id, v);
                scene->SetLightRotation(id, Quaternion::EulerAngles(180_deg, 0, -lightRot));
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

        //{
        //    auto lightId = scene->CreatePointLight();
        //    scene->SetPointLightColor(lightId, { 1_xyz, 15 });
        //    scene->SetPointLightPosition(lightId, 3_up);
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

        auto swapchain = Swapchain(&window, { .VerticalSync = true, .MaxFrames = 3 });

        auto frames
            = irange(swapchain.MaxFrames)
            | vs::transform([=](auto) { return make_unique<Frame>(device); })
            | to_vector
            ;

        auto rtp = make_unique<RenderTexPipeline>(device);
        
        rtp->Bindings.Sampler = scene->Sampler;

        auto screenCenter = (Vector2)window.Size / 2;
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
            { GLFW_KEY_3, scene->Textures[cam->NormalRoughnessId] },
            { GLFW_KEY_4, scene->Textures[cam->PositionId] },
            { GLFW_KEY_5, scene->Textures[cam->AmbientOcclusionId] },
            { GLFW_KEY_6, scene->Textures[cam->DepthId] },
        };

        for (auto [i, mip] : scene->Textures[cam->BloomId]->Mipchain | vs::take(9) | indexed32)
            textureTargets.emplace_back(GLFW_KEY_KP_0 + i, mip);

        u32 screenShotCount = 0;
        rtp->Bindings.Textures = textureTargets | vs::values | to_vector;

        auto update = [&, Time = Time()]() mutable
        {
            Window::PollEvents();
            if (window.ShouldClose())
                std::exit(0);
            Time.Update();

            {
                lightRotation += Time.Delta * 45_deg;
                updateLightPos();
                //auto sqrBall = MeshInstanceId(1);
                //scene->SetMeshDataShapeDelta(scene->MeshInstances[sqrBall].DataId, 0, PingPong(Time.Elapsed * 5, 5.f) - 2.5f);
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
                delta = cam->Transform->Rotation.RotationMatrix * delta;
                delta.y = 0;
                if (delta.Magnitude > 0)
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
                    //scene->SetPointLightPosition(lightId, scene->PointLights[lightId].Position + 3 * lightDelta * Time.Delta);
                }

                for (auto [i, key, tex] : textureTargets | indexed32) if (window.GetKey(key))
                {
                    rtp->TextureIndex = i;
                    break;
                }

                cam->Transform->Position += delta * (Time.Delta * 3);
                cam->Transform->Rotation = slerp(cam->Transform->Rotation, camRot(), Time.Delta * 20);
            }

            if (exrTask.valid() && exrTask.wait_for(0s) == std::future_status::ready)
            {
                scene->EnvironmentTexture = exrId;
                exrTask = {};
            }

        };

        for (auto frameCount : vs::iota(0))
        {
            auto frame = frames[frameCount % frames.size()].get();

            SwapchainTexture* swapTex;
            do //Continually update our app.
            {
                update();
                tie(swapTex) = frame->Begin(&swapchain);
            } while (!swapTex);

            auto beginTime = high_resolution_clock::now();

            scene->Update(frame);
            auto updateTime = high_resolution_clock::now();

            scene->Render(frame);
            auto renderTime = high_resolution_clock::now();

            rtp->Output->Target = swapTex;
            rtp->IsDepth        = rtp->TextureIndex == 5;
            //rtp->IsSpecular     = rtp->TextureIndex == 3 + 2;
            rtp->IsAO           = rtp->TextureIndex == 4;
            //rtp->IsPackedNormal = rtp->TextureIndex == 2;
            rtp->CorrectGamma   = rtp->TextureIndex <= 1;
            rtp->UseTonemap     = rtp->TextureIndex <= 1 || rtp->TextureIndex == 3;
            rtp->RenderAlpha    = window.GetKey(GLFW_KEY_Q);
            rtp->Near           = .01f;
            rtp->Far            = 100;
            rtp->Begin(frame);
                rtp->DrawTriangle();
            rtp->End();

            instanceImGui.OutputColor->Target = swapTex;
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
                        TextF("Update({}), Render({})", duration_cast<std::chrono::milliseconds>(updateTime - beginTime), duration_cast<std::chrono::milliseconds>(renderTime - beginTime));
                        Separator();

                        Camera(scene.get(), camId);

                        Image(scene->Textures[cam->OutputId], cam->ScreenSize / 5);
                        SameLine();
                        Image(scene->Textures[cam->NormalRoughnessId], cam->ScreenSize / 5);

                        Image(scene->Textures[cam->AmbientOcclusionId], cam->ScreenSize / 5);
                        SameLine();
                        Image(scene->Textures[cam->AlbedoMetallicId], cam->ScreenSize / 5);

                    }
                    End();
                    if (Begin("Transform"))
                    {
                        for (auto& tr : scene->Transforms)
                        {
                            Separator();
                            Transform(&tr);
                        }
                    }
                    End();
                    if (Begin("Textures"))
                    {
                        for (auto& tr : scene->Textures)
                        {
                            Separator();
                            Image(tr, 128_xy);
                        }
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
                rtp->Output->Target = screenShot.get();
                rtp->Begin(frame);
                    rtp->DrawTriangle();
                rtp->End();
            }

            frame->End();
            threadPool.Submit([frame, q = device->AcquireQueue(0)] { q->Submit(frame); }).wait(); //Waiting, since Scene3D is not ready for async rendering.

            if (screenShot)
                screenShot->Save(Assets / "ScreenShoot{}.png"_f(++screenShotCount));
        }
    }
    //catch (const std::exception& e)
    //{
    //    println("{}", e.what());
    //}
}