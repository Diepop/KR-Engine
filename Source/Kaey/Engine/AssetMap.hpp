#pragma once
#include "Utils.hpp"

#define KAEY_ENGINE_ASSET_MAP(type, map) \
    template<class AssetType = type, class... Args> shared_ptr<AssetType> FindOrCreate##type(fs::path path, Args&&... args) { return map.FindOrCreateShared<AssetType>(move(path), std::forward<Args>(args)...); } \
    const fs::path& PathOf(type* asset) const { return map.PathOf(asset); } \
    string_view NameOf(type* asset) const { return map.NameOf(asset); } \
    shared_ptr<type> SharedOf(type* asset) const { return map.SharedOf(asset); }

namespace Kaey::Engine
{
    template<class Asset>
    struct AssetMap
    {
        struct AssetItem
        {
            Asset* Ptr;
            const string* NamePtr;
            const fs::path* PathPtr;
            shared_ptr<Asset> Shared;
            i32 Index;
        };

        template<class AssetType = Asset, class... Args>
        shared_ptr<AssetType> FindOrCreateShared(fs::path path, Args&&... args)
        {
            auto it = assetList.end();
            bool load = false;
            {
                path = absolute(path);
                auto l = lock_guard(mut);
                it = rn::find_if(assetList, [=](auto& t) { return *t.PathPtr == path; });
                load = it == assetList.end();
                if (load)
                {
                    auto namePtr = AddUniqueName(path);
                    auto pathPtr = AddPath(move(path));
                    it = assetList.insert(it, AssetItem{ nullptr, namePtr, pathPtr, {  }, -1 });
                    pathMap.emplace(pathPtr, &*it);
                    nameMap.emplace(*namePtr, &*it);
                }
            }
            if (load)
            {
                auto ptr = make_shared<AssetType>(forward<Args>(args)...);
                ptr->Engine->SubmitSyncronized([&, ptr, it]
                {
                    auto l = lock_guard(mut);
                    auto vIt = rn::find(assets, nullptr);
                    if (vIt != assets.end())
                    {
                        it->Index = i32(vIt - assets.begin());
                        assets[it->Index] = ptr.get();
                    }
                    else
                    {
                        it->Index = i32(assets.size());
                        assets.emplace_back(ptr.get());
                    }
                    assetMap.emplace(ptr.get(), &*it);
                    it->Shared = ptr;
                    it->Ptr = ptr.get();
                });
                return ptr;
            }
            while (!it->Ptr)
                std::this_thread::sleep_for(1ns);
            if constexpr (std::is_same_v<AssetType, Asset>)
                return it->Shared;
            else return std::static_pointer_cast<AssetType>(it->Shared);
        }

        const fs::path& PathOf(Asset* asset) const
        {
            auto l = lock_guard(mut);
            auto it = assetMap.find(asset);
            return it != assetMap.end() ? *it->second->PathPtr : emptyPath;
        }

        string_view NameOf(Asset* asset) const
        {
            auto l = lock_guard(mut);
            auto it = assetMap.find(asset);
            return it != assetMap.end() ? *it->second->NamePtr : string_view();
        }

        shared_ptr<Asset> SharedOf(Asset* asset) const
        {
            auto l = lock_guard(mut);
            auto it = assetMap.find(asset);
            return it != assetMap.end() ? it->second->Shared : nullptr;
        }

        i32 IndexOf(Asset* asset) const
        {
            auto l = lock_guard(mut);
            auto it = assetMap.find(asset);
            return it != assetMap.end() ? it->second->Index : -1;
        }

        void Update()
        {
            auto l = lock_guard(mut);
            vector<Asset*> toRemove;
            for (auto& item : assetList) if (item.Shared.use_count() == 1)
                toRemove.emplace_back(item.Ptr);
            for (auto asset : toRemove)
                UnRegisterUnlocked(asset);
        }

        KAEY_ENGINE_GETTER(cspan<Asset*>, Assets) { return assets; }

    private:
        list<AssetItem> assetList;

        vector<Asset*> assets;

        unordered_set<string> names;
        unordered_set<fs::path> paths;

        unordered_map<string_view, const AssetItem*> nameMap;
        unordered_map<const fs::path*, const AssetItem*> pathMap;
        unordered_map<Asset*, const AssetItem*> assetMap;

        fs::path emptyPath;

        mutable mutex mut;

        void UnRegisterUnlocked(Asset* asset)
        {
            auto it = assetMap.find(asset);
            assert(it != assetMap.end());
            auto& [ptr, namePtr, pathPtr, shared, index] = *it->second;
            assets[index] = nullptr;
            nameMap.erase(*namePtr);
            pathMap.erase(pathPtr);
            names.erase(*namePtr);
            paths.erase(*pathPtr);
            assetList.erase(rn::find_if(assetList, [=](auto& item) { return item.Ptr == asset; }));
        }

        const string* AddUniqueName(string name)
        {
            auto [it, added] = names.emplace(move(name));
            if (!added)
            {
                auto& n = *it;
                auto i = 0;
                do tie(it, added) = names.emplace("{}.{:03}"_f(n, ++i));
                while (!added);
            }
            return &*it;
        }

        const string* AddUniqueName(const fs::path& path)
        {
            return AddUniqueName(path.stem().string());
        }

        const fs::path* AddPath(fs::path path = {})
        {
            return &*paths.emplace(move(path)).first;
        }

    };

}