#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace av {

using Entity = std::uint32_t;
inline constexpr Entity kNullEntity = 0;

// Minimal entity-component registry. Entities are plain ids; components are
// stored per type in node-based maps, so a component reference stays valid
// until that component (or its entity) is removed. Systems iterate the
// components they need with each<T>() or entitiesWith<T>().
//
// Adding a component of type T while iterating T with each<T>() is not
// allowed (the map may rehash); iterate a copy from entitiesWith<T>() instead.
class Registry {
public:
    Entity create()
    {
        const Entity e = next_++;
        alive_.insert(e);
        return e;
    }

    void destroy(Entity e)
    {
        for (auto& pool : pools_) {
            if (pool) {
                pool->remove(e);
            }
        }
        alive_.erase(e);
    }

    bool valid(Entity e) const { return alive_.count(e) != 0; }
    std::size_t size() const { return alive_.size(); }

    template <typename T>
    T& add(Entity e, T component = T{})
    {
        return pool<T>().items.insert_or_assign(e, std::move(component)).first->second;
    }

    template <typename T>
    T* get(Entity e)
    {
        Pool<T>* p = find<T>();
        if (p == nullptr) {
            return nullptr;
        }
        const auto it = p->items.find(e);
        return it == p->items.end() ? nullptr : &it->second;
    }

    template <typename T>
    const T* get(Entity e) const
    {
        return const_cast<Registry*>(this)->get<T>(e);
    }

    template <typename T>
    bool has(Entity e) const
    {
        return get<T>(e) != nullptr;
    }

    template <typename T>
    void remove(Entity e)
    {
        if (Pool<T>* p = find<T>()) {
            p->items.erase(e);
        }
    }

    template <typename T, typename F>
    void each(F&& f)
    {
        if (Pool<T>* p = find<T>()) {
            for (auto& [e, c] : p->items) {
                f(e, c);
            }
        }
    }

    template <typename T>
    std::vector<Entity> entitiesWith() const
    {
        std::vector<Entity> out;
        if (const Pool<T>* p = const_cast<Registry*>(this)->find<T>()) {
            out.reserve(p->items.size());
            for (const auto& [e, c] : p->items) {
                out.push_back(e);
            }
        }
        return out;
    }

    template <typename T>
    std::size_t count() const
    {
        const Pool<T>* p = const_cast<Registry*>(this)->find<T>();
        return p ? p->items.size() : 0;
    }

private:
    struct IPool {
        virtual ~IPool() = default;
        virtual void remove(Entity e) = 0;
    };
    template <typename T>
    struct Pool final : IPool {
        std::unordered_map<Entity, T> items;
        void remove(Entity e) override { items.erase(e); }
    };

    static std::size_t nextTypeId()
    {
        static std::size_t counter = 0;
        return counter++;
    }
    template <typename T>
    static std::size_t typeId()
    {
        static const std::size_t id = nextTypeId();
        return id;
    }

    template <typename T>
    Pool<T>& pool()
    {
        const std::size_t id = typeId<T>();
        if (id >= pools_.size()) {
            pools_.resize(id + 1);
        }
        if (!pools_[id]) {
            pools_[id] = std::make_unique<Pool<T>>();
        }
        return static_cast<Pool<T>&>(*pools_[id]);
    }

    template <typename T>
    Pool<T>* find()
    {
        const std::size_t id = typeId<T>();
        return id < pools_.size() && pools_[id] ? static_cast<Pool<T>*>(pools_[id].get()) : nullptr;
    }

    std::vector<std::unique_ptr<IPool>> pools_;
    std::unordered_set<Entity> alive_;
    Entity next_ = 1;
};

} // namespace av
