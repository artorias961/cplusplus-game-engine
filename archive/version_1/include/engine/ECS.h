#pragma once
// ---------------------------------------------------------------------------
// ECS.h — a minimal Entity-Component-System.
//
// The idea: instead of a class hierarchy (GameObject -> Character -> Player),
// game objects are just IDs ("entities"). Data lives in small plain structs
// ("components") stored in flat maps, keyed by entity. "Systems" are just
// ordinary functions that loop over the components they care about.
//
// This version favors clarity over raw performance: it uses
// std::unordered_map instead of tightly packed arrays. That's the right
// trade-off for learning the pattern; a production engine would replace the
// storage internals with contiguous arrays ("sparse sets") without changing
// this public API at all.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace engine {

// An entity is nothing but a number. It has no behavior and no data of its
// own — it's just a key used to look components up.
using Entity = std::uint32_t;
constexpr Entity kInvalidEntity = 0;

// --- Component storage -----------------------------------------------------

// World needs to hold pools of *different* component types (Transform pool,
// Velocity pool, Sprite pool, ...) in one container. To do that it needs a
// common, non-templated base class it can store a pointer to.
class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
};

// ComponentPool<T> is where every instance of component type T actually
// lives: one T per entity that has one, in a map from entity -> T.
template <typename T>
class ComponentPool : public IComponentPool {
public:
    void add(Entity e, T component) { data_[e] = std::move(component); }

    void remove(Entity e) override { data_.erase(e); }

    bool has(Entity e) const { return data_.find(e) != data_.end(); }

    T* get(Entity e) {
        auto it = data_.find(e);
        return it == data_.end() ? nullptr : &it->second;
    }

    // Exposes the raw map so systems can iterate "every entity that has a T".
    std::unordered_map<Entity, T>& all() { return data_; }

private:
    std::unordered_map<Entity, T> data_;
};

// --- The World ---------------------------------------------------------
//
// World is the single object that owns every entity and every component.
// Game code and engine systems both talk to the World, never to each other
// directly — that's what keeps rendering, physics, input, etc. decoupled.
class World {
public:
    Entity createEntity() {
        Entity e = nextEntity_++;
        aliveEntities_.push_back(e);
        return e;
    }

    // Destroys an entity immediately: every component it owns is erased.
    //
    // "Immediately" is the catch. Erasing from a pool invalidates iterators
    // into that pool, so calling this while a system is looping over a
    // view<T>() corrupts the loop that is running. Inside a system, or
    // anywhere you aren't certain, use destroyLater() instead.
    void destroyEntity(Entity e) {
        for (auto& [type, pool] : pools_) {
            pool->remove(e);
        }
        aliveEntities_.erase(
            std::remove(aliveEntities_.begin(), aliveEntities_.end(), e),
            aliveEntities_.end());
    }

    // Queues an entity to be destroyed at the end of the frame. This is the
    // safe default and the one real engines expose: code that decides
    // something should die ("this enemy ran out of health") is almost always
    // in the middle of iterating over the very pool the entity lives in.
    // Deferring means the decision and the deletion happen at different
    // times, and only the deletion needs to care about iterator safety.
    void destroyLater(Entity e) { pendingDestroy_.push_back(e); }

    // Applies every queued destruction. The engine calls this once a frame,
    // after game logic and before rendering — a point where nothing is
    // iterating a pool, so erasing is safe. Queuing the same entity twice is
    // harmless: the second destroyEntity finds nothing left to erase.
    void flushDestroyed() {
        for (Entity e : pendingDestroy_) {
            destroyEntity(e);
        }
        pendingDestroy_.clear();
    }

    template <typename T>
    void addComponent(Entity e, T component) {
        getPool<T>().add(e, std::move(component));
    }

    template <typename T>
    T* getComponent(Entity e) {
        return getPool<T>().get(e);
    }

    template <typename T>
    bool hasComponent(Entity e) {
        return getPool<T>().has(e);
    }

    // Returns every (entity, component) pair for type T. Use this to write a
    // "system": a plain loop like
    //     for (auto& [entity, transform] : world.view<Transform>()) { ... }
    template <typename T>
    std::unordered_map<Entity, T>& view() {
        return getPool<T>().all();
    }

    const std::vector<Entity>& entities() const { return aliveEntities_; }

private:
    // Finds (or lazily creates) the ComponentPool for type T. Each distinct
    // T gets exactly one pool, looked up by its std::type_index.
    template <typename T>
    ComponentPool<T>& getPool() {
        auto type = std::type_index(typeid(T));
        auto it = pools_.find(type);
        if (it == pools_.end()) {
            auto pool = std::make_unique<ComponentPool<T>>();
            ComponentPool<T>* raw = pool.get();
            pools_.emplace(type, std::move(pool));
            return *raw;
        }
        return *static_cast<ComponentPool<T>*>(it->second.get());
    }

    // Known limitation: IDs are handed out by counting up and are never
    // reused, so a long-running game leaks ID space (4 billion entities in,
    // this wraps). The standard fix is a free list plus a "generation"
    // counter packed into the ID, so a stale copy of a destroyed entity's ID
    // can be detected instead of silently addressing whatever took its slot.
    // That turns Entity from a plain uint32_t into a small handle struct —
    // the right call for a real engine, and a poor trade here, where being
    // able to say "an entity is just a number" is the point.
    Entity nextEntity_ = 1;  // 0 is reserved as "invalid"
    std::vector<Entity> aliveEntities_;
    std::vector<Entity> pendingDestroy_;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
};

}  // namespace engine
