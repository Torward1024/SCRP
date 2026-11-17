#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <scrp/c/Physics3D.h>
#include "CApiInternal.h"
#include <atomic>
#include <memory>
#include <unordered_map>

namespace scrp {
namespace {
struct JoltRuntime {
    JoltRuntime() {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ~JoltRuntime() { JPH::UnregisterTypes(); delete JPH::Factory::sInstance; JPH::Factory::sInstance = nullptr; }
};
void initialize() { static JoltRuntime runtime; (void)runtime; }
std::atomic<uint64_t> next_body{1};
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer(static_cast<JPH::uint8>(layer)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() ? "Dynamic" : "Static"; }
#endif
};
class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override { return object != 0 || broad.GetValue() != 0; }
};
class PairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a != 0 || b != 0; }
};
JPH::Vec3 vec(scrp_vec3 v) { return {v.x,v.y,v.z}; }
JPH::RVec3 point(scrp_vec3 v) { return {v.x,v.y,v.z}; }
scrp_vec3 from(JPH::Vec3 v) { return {v.GetX(),v.GetY(),v.GetZ()}; }
JPH::Quat quat(scrp_quat q) { return JPH::Quat(q.x,q.y,q.z,q.w).Normalized(); }
bool range(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
}
struct PhysicsWorld {
    BroadLayers layers;
    BroadFilter broad_filter;
    PairFilter pair_filter;
    JPH::TempAllocatorMalloc allocator;
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    JPH::PhysicsSystem system;
    std::unordered_map<scrp_body_id,JPH::BodyID> bodies;
    uint32_t capacity = 0;
    explicit PhysicsWorld(const scrp_world_desc& desc) {
        capacity = desc.max_bodies;
        system.Init(capacity,0,capacity*8,capacity*4,layers,broad_filter,pair_filter);
        system.SetGravity(vec(desc.gravity));
        auto settings = system.GetPhysicsSettings();
        settings.mPenetrationSlop = 0.002f;
        system.SetPhysicsSettings(settings);
        bodies.reserve(capacity);
    }
    ~PhysicsWorld() {
        auto& api = system.GetBodyInterface();
        for (auto item : bodies) { api.RemoveBody(item.second); api.DestroyBody(item.second); }
    }
    JPH::BodyID find(scrp_body_id id) const {
        const auto it = bodies.find(id);
        return it == bodies.end() ? JPH::BodyID() : it->second;
    }
};
}
struct scrp_world { std::unique_ptr<scrp::PhysicsWorld> impl; };

extern "C" {
int scrp_world_create(const scrp_world_desc *desc, scrp_world **out, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!out || *out || !desc || !scrp::capi::bounded(desc->gravity,1e4f) || desc->max_bodies < 2 || desc->max_bodies > 65536)
            return scrp::capi::fail(error,"World requires finite gravity, capacity 2..65536 and an empty output handle");
        scrp::initialize();
        auto world = std::make_unique<scrp_world>();
        world->impl = std::make_unique<scrp::PhysicsWorld>(*desc);
        *out = world.release();
        return 1;
    });
}
void scrp_world_destroy(scrp_world **world) {
    if (world && *world) { delete *world; *world = nullptr; }
}
int scrp_world_add_box(scrp_world *world, const scrp_body_desc *desc, scrp_body_id *out, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out = 0;
        if (!world || !desc || !out || !scrp::capi::valid(desc->pose) ||
            !scrp::range(desc->half_extent.x,0.01f,10000) || !scrp::range(desc->half_extent.y,0.01f,10000) ||
            !scrp::range(desc->half_extent.z,0.01f,10000) || !(desc->mass==0 || scrp::range(desc->mass,0.001f,1e8f)) ||
            !scrp::range(desc->friction,0,2) || !scrp::range(desc->restitution,0,1) ||
            !scrp::range(desc->linear_damping,0,10) || !scrp::range(desc->angular_damping,0,10))
            return scrp::capi::fail(error,"Invalid box pose, dimensions or physical parameters");
        auto& w = *world->impl;
        if (w.bodies.size() >= w.capacity) return scrp::capi::fail(error,"World body capacity reached");
        JPH::BoxShapeSettings shape(scrp::vec(desc->half_extent),0.0f);
        auto result = shape.Create();
        if (result.HasError()) return scrp::capi::fail(error,result.GetError().c_str());
        const bool dynamic = desc->mass > 0;
        JPH::BodyCreationSettings settings(result.Get(),scrp::point(desc->pose.position),scrp::quat(desc->pose.rotation),
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, dynamic ? 1 : 0);
        settings.mFriction = desc->friction;
        settings.mRestitution = desc->restitution;
        settings.mLinearDamping = desc->linear_damping;
        settings.mAngularDamping = desc->angular_damping;
        settings.mMotionQuality = dynamic && desc->continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        if (dynamic) { settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia; settings.mMassPropertiesOverride.mMass = desc->mass; }
        const auto handle = scrp::next_body.fetch_add(1);
        settings.mUserData = handle;
        auto& api = w.system.GetBodyInterface();
        const auto id = api.CreateAndAddBody(settings,dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        if (id.IsInvalid()) return scrp::capi::fail(error,"Unable to allocate a physics body");
        try { w.bodies.emplace(handle,id); }
        catch (...) { api.RemoveBody(id); api.DestroyBody(id); throw; }
        *out = handle;
        return 1;
    });
}
int scrp_world_remove_body(scrp_world *world, scrp_body_id body, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!world) return scrp::capi::fail(error,"World is required");
        auto& w = *world->impl;
        const auto id = w.find(body);
        if (id.IsInvalid()) return scrp::capi::fail(error,"Unknown body in this world");
        auto& api = w.system.GetBodyInterface();
        api.RemoveBody(id); api.DestroyBody(id); w.bodies.erase(body);
        return 1;
    });
}
int scrp_world_body_state(const scrp_world *world, scrp_body_id body, scrp_body_state *out, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!world || !out) return scrp::capi::fail(error,"World and state output are required");
        const auto id = world->impl->find(body);
        if (id.IsInvalid()) return scrp::capi::fail(error,"Unknown body in this world");
        const auto& api = world->impl->system.GetBodyInterface();
        const auto p = api.GetPosition(id);
        const auto q = api.GetRotation(id);
        *out = {{{float(p.GetX()),float(p.GetY()),float(p.GetZ())},{q.GetX(),q.GetY(),q.GetZ(),q.GetW()}},
                scrp::from(api.GetLinearVelocity(id)),scrp::from(api.GetAngularVelocity(id)),api.IsActive(id) ? 1 : 0};
        return 1;
    });
}
int scrp_world_set_pose(scrp_world *world, scrp_body_id body, scrp_pose pose, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!world || !scrp::capi::valid(pose)) return scrp::capi::fail(error,"World and a finite unit pose are required");
        auto& w = *world->impl;
        const auto id = w.find(body);
        if (id.IsInvalid()) return scrp::capi::fail(error,"Unknown body in this world");
        auto& api = w.system.GetBodyInterface();
        api.SetPositionAndRotation(id,scrp::point(pose.position),scrp::quat(pose.rotation),JPH::EActivation::Activate);
        if (api.GetMotionType(id) == JPH::EMotionType::Dynamic) {
            api.SetLinearAndAngularVelocity(id,JPH::Vec3::sZero(),JPH::Vec3::sZero());
            JPH::BodyLockWrite lock(w.system.GetBodyLockInterface(),id);
            if (lock.Succeeded()) lock.GetBody().ResetForce();
            if (lock.Succeeded()) lock.GetBody().ResetTorque();
        }
        return 1;
    });
}
int scrp_world_apply_force(scrp_world *world, scrp_body_id body, scrp_vec3 force, scrp_vec3 point, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!world || !scrp::capi::bounded(force,1e9f) || !scrp::capi::bounded(point,1e6f)) return scrp::capi::fail(error,"World, bounded force and point are required");
        const auto id = world->impl->find(body);
        if (id.IsInvalid()) return scrp::capi::fail(error,"Unknown body in this world");
        auto& api = world->impl->system.GetBodyInterface();
        if (api.GetMotionType(id) == JPH::EMotionType::Dynamic) api.AddForce(id,scrp::vec(force),scrp::point(point));
        return 1;
    });
}
int scrp_world_apply_impulse(scrp_world *world, scrp_body_id body, scrp_vec3 impulse, scrp_vec3 point, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!world || !scrp::capi::bounded(impulse,1e9f) || !scrp::capi::bounded(point,1e6f)) return scrp::capi::fail(error,"World, bounded impulse and point are required");
        const auto id = world->impl->find(body);
        if (id.IsInvalid()) return scrp::capi::fail(error,"Unknown body in this world");
        auto& api = world->impl->system.GetBodyInterface();
        if (api.GetMotionType(id) == JPH::EMotionType::Dynamic) api.AddImpulse(id,scrp::vec(impulse),scrp::point(point));
        return 1;
    });
}
int scrp_world_step(scrp_world *world, float dt, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (!world || !scrp::range(dt,1e-6f,1.0f/30.0f)) return scrp::capi::fail(error,"Physics step must be finite and in 0..1/30 second");
        auto& w = *world->impl;
        if (w.system.Update(dt,1,&w.allocator,&w.jobs) != JPH::EPhysicsUpdateError::None)
            return scrp::capi::fail(error,"Physics capacity exceeded during update");
        return 1;
    });
}
int scrp_world_raycast(const scrp_world *world, scrp_vec3 origin, scrp_vec3 displacement, scrp_body_id ignore, scrp_ray_hit *out, scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out = {};
        if (!world || !out || !scrp::capi::bounded(origin,1e6f) || !scrp::capi::bounded(displacement,1e6f) || scrp_vec3_dot(displacement,displacement) < 1e-12f)
            return scrp::capi::fail(error,"Ray requires a world, output, finite origin and nonzero displacement");
        auto& w = *world->impl;
        const auto skip = w.find(ignore);
        if (ignore && skip.IsInvalid()) return scrp::capi::fail(error,"Unknown ignored body in this world");
        JPH::RRayCast ray(scrp::point(origin),scrp::vec(displacement));
        JPH::RayCastResult hit;
        if (!w.system.GetNarrowPhaseQuery().CastRay(ray,hit,{}, {}, JPH::IgnoreSingleBodyFilter(skip))) return 1;
        JPH::BodyLockRead lock(w.system.GetBodyLockInterface(),hit.mBodyID);
        if (!lock.Succeeded()) return scrp::capi::fail(error,"Unable to read hit body");
        const auto p = ray.GetPointOnRay(hit.mFraction);
        const auto& b = lock.GetBody();
        *out = {1,b.GetUserData(),hit.mFraction,{float(p.GetX()),float(p.GetY()),float(p.GetZ())},
                scrp::from(b.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,p)),scrp::from(b.GetPointVelocity(p))};
        return 1;
    });
}
const char *scrp_physics_backend(void) { return "Jolt 5.3.0 (0373ec0dd762e4bc2f6acdb08371ee84fa23c6db)"; }
}
