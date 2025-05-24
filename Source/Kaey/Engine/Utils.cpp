#include "Utils.hpp"
#include "MeshData.hpp"
#include "Shaders.hpp"
#include "Texture.hpp"

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger)
{
    static auto ptr = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    assert(ptr);
    return ptr(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator)
{
    static auto ptr = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    assert(ptr);
    return ptr(instance, messenger, pAllocator);
}

namespace Engine
{
    namespace Utils
    {
        mutex PrintMutex;
    }

    void CantFail(vk::Result result, const char* msg)
    {
        if (result != vk::Result::eSuccess)
            throw runtime_error(msg);
    }

    vk::UniqueShaderModule LoadShader(vk::Device device, cspan<u8> byteCode)
    {
        return device.createShaderModuleUnique({ {}, (u32)byteCode.size(), (const u32*)byteCode.data() });  // NOLINT(clang-diagnostic-cast-align)
    }

    vk::UniqueShaderModule LoadShader(vk::Device device, const fs::path& path)
    {
        using namespace std;
        ifstream file(path, ios::ate | ios::binary);
        if (!file.is_open())
            throw runtime_error("Failed to open file: " + path.string());
        vector<u8> buffer;
        buffer.resize(file.tellg());
        file.seekg(0);
        file.read((char*)buffer.data(), (streamsize)buffer.size());
        file.close();
        return LoadShader(device, buffer);
    }

    Shaders LoadShaders(vk::Device device, vector<fs::path> shaderPaths)
    {
        Shaders result;
        auto& [shaders, infos] = result;
        for (auto& path : shaderPaths)
        {
            auto ext = path.extension();
            path.replace_extension(ext.string() + ".spv");
            auto& shader = shaders.emplace_back(LoadShader(device, path));
            auto stage =
                ext == ".vert" ? vk::ShaderStageFlagBits::eVertex :
                ext == ".frag" ? vk::ShaderStageFlagBits::eFragment :
                ext == ".comp" ? vk::ShaderStageFlagBits::eCompute :
                throw runtime_error("Shader type is not valid!");
            infos.push_back({ {}, stage, shader.get(), "main" });
        }
        return result;
    }

    Shaders LoadShaders(vk::Device device, vector<pair<cspan<u8>, vk::ShaderStageFlagBits>> shaderDatas)
    {
        Shaders result;
        auto& [shaders, infos] = result;
        for (auto& [byteCode, stage] : shaderDatas)
        {
            auto& shader = shaders.emplace_back(LoadShader(device, byteCode));
            infos.push_back({ {}, stage, shader.get(), "main" });
        }
        return result;
    }

    u32 FindMemoryIndex(vk::PhysicalDevice physicalDevice, u32 typeFilter, vk::MemoryPropertyFlags properties)
    {
        auto memProperties = physicalDevice.getMemoryProperties();
        for (u32 i = 0; i < memProperties.memoryTypeCount; ++i)
            if (typeFilter & 1 << i && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        throw runtime_error("Failed to find suitable memory type!");
    }

    size_t hash_value(const Vertex& obj)
    {
        size_t seed = 0x0314DF12;
        hash_combine(seed, obj.Position);
        hash_combine(seed, obj.Normal);
        //hash_combine(seed, obj.Uv);
        //hash_combine(seed, obj.Tangent);
        //hash_combine(seed, obj.BiTangent);
        return seed;
    }

    bool operator==(const Vertex& lhs, const Vertex& rhs)
    {
        return lhs.Position == rhs.Position
            && lhs.Normal == rhs.Normal
            //&& lhs.Uv == rhs.Uv
            //&& lhs.Tangent == rhs.Tangent
            //&& lhs.BiTangent == rhs.BiTangent
            ;
    }

    bool operator!=(const Vertex& lhs, const Vertex& rhs)
    {
        return !(lhs == rhs);
    }

}

size_t std::hash<Engine::Vertex>::operator()(const Engine::Vertex& v) const noexcept
{
    return hash_value(v);
}

namespace ImGui
{
    void Text(std::string_view str)
    {
        Text("%.*s", str.size(), str.data());
    }

    void TextUnformatted(std::string_view str)
    {
        return TextUnformatted(str.data(), str.data() + str.size());
    }

    bool TreeNode(std::string_view str)
    {
        return TreeNode((void*)Engine::chash(str), "%.*s", str.size(), str.data());
    }

    bool TreeNode(const std::string& str)
    {
        return TreeNode(std::string_view(str));
    }

    void Image(Engine::Texture* tex, const Engine::Vector2& size)
    {
        if (!tex->DescriptorSet)
            return;
        if (size == Engine::Vector2::Zero)
        {
            auto&& [w, h] = tex->Extent;
            return Image(tex->DescriptorSet, { float(w), float(h) });
        }
        return Image(tex->DescriptorSet, { size.x, size.y });
    }

    bool ImageButton(Engine::Texture* tex, const Engine::Vector2& size)
    {
        if (!tex->DescriptorSet)
            return false;
        if (size == Engine::Vector2::Zero)
        {
            auto&& [w, h] = tex->Extent;
            return ImageButton(tex->DescriptorSet, { float(w), float(h) }, { 0, 0 }, { 1, 1 }, 0);
        }
        return ImageButton(tex->DescriptorSet, { size.x, size.y }, { 0, 0 }, { 1, 1 }, 0);
    }

    bool InputText(std::string_view label, std::filesystem::path& path, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
    {
        static char buffer[0x100]{};
        std::ranges::copy_n(path.string().c_str(), 0x99, buffer);
        if (InputText(label.data(), buffer, 0x100, flags, callback, userData))
        {
            path = buffer;
            return true;
        }
        return false;
    }

    bool InputText(std::string_view label, std::string& str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* userData)
    {
        static char buffer[0x100]{};
        std::ranges::copy_n(str.c_str(), 0x99, buffer);
        if (InputText(label.data(), buffer, 0x100, flags, callback, userData))
        {
            str = buffer;
            return true;
        }
        return false;
    }

    bool TreeNode(const std::filesystem::path& path)
    {
        return TreeNode(path.string());
    }

    bool Checkbox(const char* label, bool value)
    {
        return Checkbox(label, &value);
    }

    void MaterialEdit(Engine::Material* mat)
    {
        if (!mat)
        {
            Text("No Material");
            return;
        }
        if (!TreeNode(mat->Name))
            return;
        mat->OnGui();
        TreePop();
    }
}
