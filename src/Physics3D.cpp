#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <scrp/c/Physics3D.h>
#include "CApiInternal.h"
#include <atomic>
#include <algorithm>
#include <memory>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <vector>

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

struct ImpactCandidate {
    scrp_impact event;
    struct Sweep {
        JPH::BodyID body;
        JPH::RVec3 center_at_hit;
        JPH::Vec3 direction;
    } sweeps[2];
};
using BodyPair = std::pair<scrp_body_id,scrp_body_id>;
BodyPair pair_of(const scrp_impact& e) { return {e.body_a,e.body_b}; }

// The world currently owns centered boxes. Their support planes let us locate
// the hit in both local frames before Jolt advances a linear-cast body to TOI.
float support_radius(const JPH::Body& b,JPH::Vec3 local_normal) {
    return b.GetShape()->GetLocalBounds().GetExtent().Dot(local_normal.Abs());
}
JPH::Vec3 cast_velocity(const JPH::Body& b) {
    return b.IsDynamic() && b.GetMotionProperties()->GetMotionQuality()==JPH::EMotionQuality::LinearCast
        ? b.GetLinearVelocity() : JPH::Vec3::sZero();
}
float inverse_effective_mass(const JPH::Body& b,JPH::Vec3 arm,JPH::Vec3 normal) {
    if (!b.IsDynamic()) return 0;
    const auto torque=arm.Cross(normal);
    return b.GetMotionProperties()->GetInverseMass() + torque.Dot(b.GetInverseInertia().Multiply3x3(torque));
}

class ImpactListener final : public JPH::ContactListener {
public:
    std::vector<scrp_impact> snapshot;
    float dt=0, restitution_threshold=1, cast_threshold=.75f;

    void begin(float step_dt) {
        dt=step_dt;
        pending.clear();
        snapshot.clear();
        failed=false;
    }
    void OnContactAdded(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override { record(a,b,manifold,settings); }
    void OnContactPersisted(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override { record(a,b,manifold,settings); }

    bool finish(const JPH::BodyInterface& api) {
        if (failed) { previous.clear(); return false; }
        // CCD can notify both potential collisions before it orders them by
        // time and cuts one cast short. Discard a pair whose surfaces never met.
        pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const ImpactCandidate& p) {
            for (const auto& sweep:p.sweeps)
                if (!sweep.body.IsInvalid() &&
                    JPH::Vec3(api.GetCenterOfMassPosition(sweep.body)-sweep.center_at_hit).Dot(sweep.direction)<-.01f) return true;
            return false;
        }),pending.end());
        std::sort(pending.begin(),pending.end(),[](const ImpactCandidate& x,const ImpactCandidate& y) {
            const auto& a=x.event; const auto& b=y.event;
            if (pair_of(a)!=pair_of(b)) return pair_of(a)<pair_of(b);
            if (a.estimated_normal_impulse!=b.estimated_normal_impulse) return a.estimated_normal_impulse>b.estimated_normal_impulse;
            return std::tie(a.point.x,a.point.y,a.point.z,a.normal.x,a.normal.y,a.normal.z,a.closing_speed)<
                   std::tie(b.point.x,b.point.y,b.point.z,b.normal.x,b.normal.y,b.normal.z,b.closing_speed);
        });
        current.clear();
        // An allocation failure before publication leaves an empty snapshot.
        current.reserve(pending.size());
        snapshot.reserve(pending.size());
        for (const auto& p:pending) {
            const auto pair=pair_of(p.event);
            if (!current.empty() && current.back()==pair) continue;
            current.push_back(pair);
            if (!std::binary_search(previous.begin(),previous.end(),pair)) snapshot.push_back(p.event);
        }
        current.swap(previous);
        return true;
    }

    void abandon() { previous.clear(); snapshot.clear(); }

    void forget(scrp_body_id id) {
        snapshot.erase(std::remove_if(snapshot.begin(),snapshot.end(),[id](const scrp_impact& e) {
            return e.body_a==id || e.body_b==id;
        }),snapshot.end());
        previous.erase(std::remove_if(previous.begin(),previous.end(),[id](const BodyPair& p) {
            return p.first==id || p.second==id;
        }),previous.end());
    }

private:
    std::vector<ImpactCandidate> pending;
    std::vector<BodyPair> previous,current;
    std::mutex mutex;
    bool failed=false;

    ImpactCandidate::Sweep sweep_end(const JPH::Body& body,JPH::Vec3 velocity,JPH::RVec3 center_at_hit,float time) const {
        if (!time || velocity.LengthSq()*dt*dt <= JPH::Square(cast_threshold*body.GetShape()->GetInnerRadius()))
            return {JPH::BodyID(),JPH::RVec3::sZero(),JPH::Vec3::sZero()};
        return {body.GetID(),center_at_hit,velocity.Normalized()};
    }

    void record(const JPH::Body& a,const JPH::Body& b,const JPH::ContactManifold& m,const JPH::ContactSettings& settings) {
        const auto n=m.mWorldSpaceNormal;
        const auto qa=a.GetRotation().Conjugated(),qb=b.GetRotation().Conjugated();
        const auto na=qa*n,nb=qb*(-n);
        const auto com_a=a.GetCenterOfMassPosition(),com_b=b.GetCenterOfMassPosition();
        const auto va=cast_velocity(a),vb=cast_velocity(b);
        const float gap=JPH::Vec3(com_b-com_a).Dot(n)-support_radius(a,na)-support_radius(b,nb);
        const float approach=(va-vb).Dot(n);
        const bool swept=m.mPenetrationDepth>=-.00001f && gap>.0001f && approach>0;
        const float time=swept ? std::min(dt,gap/approach) : 0;
        const auto center_a=com_a+va*time,center_b=com_b+vb*time;
        JPH::Vec3 sum_a=JPH::Vec3::sZero(),sum_b=JPH::Vec3::sZero();
        float weight=0;
        for (JPH::uint i=0;i<m.mRelativeContactPointsOn1.size();++i) {
            const auto arm_a=JPH::Vec3(m.GetWorldSpaceContactPointOn1(i)-center_a);
            const auto arm_b=JPH::Vec3(m.GetWorldSpaceContactPointOn2(i)-center_b);
            const float speed=(a.GetPointVelocityCOM(arm_a)-b.GetPointVelocityCOM(arm_b)).Dot(n);
            if (speed>0) {
                sum_a+=speed*arm_a;
                sum_b+=speed*arm_b;
                weight+=speed;
            }
        }
        if (!weight) return;
        const auto arm_a=sum_a/weight,arm_b=sum_b/weight;
        const float closing=(a.GetPointVelocityCOM(arm_a)-b.GetPointVelocityCOM(arm_b)).Dot(n);
        if (closing<.5f || -m.mPenetrationDepth>closing*dt+.0001f) return;
        const float inv_mass=inverse_effective_mass(a,arm_a,n)+inverse_effective_mass(b,arm_b,n);
        if (!(inv_mass>0)) return;
        const float restitution=closing>=restitution_threshold ? settings.mCombinedRestitution : 0;
        const auto point_a=center_a+arm_a,point_b=center_b+arm_b;
        scrp_impact event{a.GetUserData(),b.GetUserData(),from(JPH::Vec3(point_a+.5f*(point_b-point_a))),from(n),
                          from(qa*arm_a),from(qb*arm_b),from(na),from(nb),closing,(1+restitution)*closing/inv_mass};
        ImpactCandidate entry{event,{sweep_end(a,va,center_a,time),sweep_end(b,vb,center_b,time)}};
        if (event.body_a>event.body_b) {
            std::swap(entry.event.body_a,entry.event.body_b);
            std::swap(entry.event.local_point_a,entry.event.local_point_b);
            std::swap(entry.event.local_normal_a,entry.event.local_normal_b);
            entry.event.normal=from(-n);
        }
        // Jolt owns the simulation locks inside these callbacks. Use the two
        // bodies already supplied; never acquire a BodyInterface lock here.
        std::lock_guard<std::mutex> guard(mutex);
        try { pending.push_back(entry); }
        catch (const std::bad_alloc&) { failed=true; }
    }
};
}
struct PhysicsWorld {
    BroadLayers layers;
    BroadFilter broad_filter;
    PairFilter pair_filter;
    JPH::TempAllocatorMalloc allocator;
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    ImpactListener impacts;
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
        impacts.restitution_threshold=settings.mMinVelocityForRestitution;
        impacts.cast_threshold=settings.mLinearCastThreshold;
        system.SetContactListener(&impacts);
        bodies.reserve(capacity);
    }
    ~PhysicsWorld() {
        system.SetContactListener(nullptr);
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
        w.impacts.forget(body);
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
        w.impacts.forget(body);
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
        w.impacts.begin(dt);
        if (w.system.Update(dt,1,&w.allocator,&w.jobs) != JPH::EPhysicsUpdateError::None) {
            w.impacts.abandon();
            return scrp::capi::fail(error,"Physics capacity exceeded during update");
        }
        if (!w.impacts.finish(w.system.GetBodyInterface()))
            return scrp::capi::fail(error,"Unable to store physics impact events");
        return 1;
    });
}
uint32_t scrp_world_impact_count(const scrp_world *world) {
    return world ? static_cast<uint32_t>(world->impl->impacts.snapshot.size()) : 0;
}
int scrp_world_impact_read(const scrp_world *world,uint32_t index,scrp_impact *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&](){
        if (out) *out={};
        if (!world || !out || index>=world->impl->impacts.snapshot.size())
            return scrp::capi::fail(error,"Unknown impact index in this world");
        *out=world->impl->impacts.snapshot[index];
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
int scrp_world_cast_cylinder(const scrp_world *world,const scrp_cylinder_cast *cast,scrp_shape_hit *out,scrp_error *error) {
    return scrp::capi::boundary(error,[&]() {
        if (out) *out={};
        if (!world || !cast || !out || !scrp::capi::valid(cast->pose) ||
            !scrp::range(cast->half_height,.001f,1000) || !scrp::range(cast->radius,.001f,1000) ||
            !scrp::capi::bounded(cast->displacement,1e6f) || scrp_vec3_dot(cast->displacement,cast->displacement)<1e-12f ||
            !scrp::capi::finite(cast->surface_axis) || std::abs(scrp_vec3_dot(cast->surface_axis,cast->surface_axis)-1)>0.002f ||
            !scrp::range(cast->min_surface_dot,-1,1)) return scrp::capi::fail(error,"Invalid cylinder cast geometry or normal filter");
        auto& w=*world->impl;
        const auto skip=w.find(cast->ignore);
        if (cast->ignore && skip.IsInvalid()) return scrp::capi::fail(error,"Unknown ignored body in this world");
        /* A small internal convex margin keeps GJK normals well conditioned at
           exact touching contact while preserving the outer dimensions. */
        const float margin=std::min(.01f,std::min(cast->half_height,cast->radius)*.1f);
        JPH::CylinderShape shape(cast->half_height,cast->radius,margin);
        shape.SetEmbedded();
        const auto origin=scrp::point(cast->pose.position);
        JPH::RShapeCast sweep(&shape,JPH::Vec3::sReplicate(1),JPH::RMat44::sRotationTranslation(scrp::quat(cast->pose.rotation),origin),scrp::vec(cast->displacement));
        JPH::ShapeCastSettings settings;
        settings.mUseShrunkenShapeAndConvexRadius=true;
        settings.mReturnDeepestPoint=true;
        class Collector final : public JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> {
        public:
            JPH::Vec3 axis; float threshold;
            Collector(JPH::Vec3 a,float t):axis(a),threshold(t) {}
            void AddHit(const JPH::ShapeCastResult& hit) override {
                if (hit.mPenetrationAxis.LengthSq()>1e-12f && (-hit.mPenetrationAxis.Normalized()).Dot(axis)>=threshold)
                    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector>::AddHit(hit);
            }
        } collector(scrp::vec(cast->surface_axis),cast->min_surface_dot);
        w.system.GetNarrowPhaseQuery().CastShape(sweep,settings,origin,collector,{}, {},JPH::IgnoreSingleBodyFilter(skip));
        if (!collector.HadHit()) return 1;
        const auto& hit=collector.mHit;
        JPH::BodyLockRead lock(w.system.GetBodyLockInterface(),hit.mBodyID2);
        if (!lock.Succeeded()) return scrp::capi::fail(error,"Unable to read cast hit body");
        const auto p=origin+hit.mContactPointOn2;
        const auto& b=lock.GetBody();
        auto normal=-hit.mPenetrationAxis.Normalized();
        const auto surface_normal=b.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,p);
        /* GJK's near-zero separation can introduce a small lateral bias on a
           large flat face. Use its exact surface normal only when both agree;
           retain the separation normal for actual edge/corner contact. */
        if (normal.Dot(surface_normal)>.9999f && surface_normal.Dot(scrp::vec(cast->surface_axis))>=cast->min_surface_dot) normal=surface_normal;
        out->surface={1,b.GetUserData(),hit.mFraction,scrp::from(JPH::Vec3(p)),scrp::from(normal),scrp::from(b.GetPointVelocity(p))};
        out->penetration_depth=hit.mFraction==0 ? hit.mPenetrationDepth : 0;
        return 1;
    });
}
}
