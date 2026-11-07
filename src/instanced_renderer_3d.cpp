#include "instanced_renderer_3d.h"
#include "godot_cpp/classes/rendering_server.hpp"
#include "instance_manager.h"
#include "godot_cpp/core/print_string.hpp"
#include <algorithm>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

static constexpr size_t PARTIAL_MAX_DIRTY = 16;
static constexpr int PARTIAL_INTERPOLATED_DIVISOR = 32;

void InstancedRenderer3D::_bind_methods() {
  ClassDB::bind_method(D_METHOD("add_instance", "transform"),
                       &InstancedRenderer3D::add_instance);
  ClassDB::bind_method(D_METHOD("update_instance", "id", "transform"),
                       &InstancedRenderer3D::update_instance);
  ClassDB::bind_method(D_METHOD("update_instances", "ids", "transforms"),
                       &InstancedRenderer3D::update_instances);
  ClassDB::bind_method(D_METHOD("remove_instance", "id"),
                       &InstancedRenderer3D::remove_instance);
  ClassDB::bind_method(D_METHOD("reset_instance_interpolation", "id"),
                       &InstancedRenderer3D::reset_instance_interpolation);
  ClassDB::bind_method(D_METHOD("is_instance_valid", "id"),
                       &InstancedRenderer3D::is_instance_valid);
  ClassDB::bind_method(D_METHOD("is_interpolated"),
                       &InstancedRenderer3D::is_interpolated);
  ClassDB::bind_method(D_METHOD("get_instance_count"),
                       &InstancedRenderer3D::get_instance_count);
  ClassDB::bind_method(D_METHOD("clear_instances"),
                       &InstancedRenderer3D::clear_instances);
  ClassDB::bind_method(D_METHOD("set_estimated_instance_count", "count"),
                       &InstancedRenderer3D::set_estimated_instance_count);
}

InstancedRenderer3D::InstancedRenderer3D() {
  RenderingServer *rs = RenderingServer::get_singleton();
  multimesh = rs->multimesh_create();
  instance = rs->instance_create();
  rs->instance_set_base(instance, multimesh);
}

InstancedRenderer3D::~InstancedRenderer3D() {
  RenderingServer *rs = RenderingServer::get_singleton();
  rs->free_rid(instance);
  rs->free_rid(multimesh);
}

void InstancedRenderer3D::setup(const RID &p_scenario, const LodLevels &p_lods,
                                const Ref<Material> &p_material,
                                int cast_shadow, uint32_t layers, bool p_static,
                                real_t visibility_range_end,
                                int initial_capacity) {
  scenario = p_scenario;
  lods = p_lods;
  material = p_material;
  is_static = p_static;
  RenderingServer *rs = RenderingServer::get_singleton();
  rs->multimesh_set_mesh(multimesh, lods[0].second.is_valid()
                                        ? lods[0].second->get_rid()
                                        : RID());
  rs->instance_geometry_set_material_override(
      instance, material.is_valid() ? material->get_rid() : RID());
  rs->instance_geometry_set_cast_shadows_setting(
      instance, (RenderingServer::ShadowCastingSetting)cast_shadow);
  rs->instance_set_layer_mask(instance, layers);
  // Godot measures this from the batch's bounds centre, not per instance.
  rs->instance_geometry_set_visibility_range(
      instance, 0, std::max<real_t>(visibility_range_end, 0), 0, 0,
      RenderingServer::VISIBILITY_RANGE_FADE_DISABLED);
  rs->instance_set_scenario(instance, scenario);
  set_estimated_instance_count(initial_capacity);
}

void InstancedRenderer3D::set_interpolation(bool p_interpolated, int quality) {
  p_interpolated = p_interpolated && !is_static;
  RenderingServer *rs = RenderingServer::get_singleton();
  if (p_interpolated != interpolated) {
    interpolated = p_interpolated;
    rs->multimesh_set_physics_interpolated(multimesh, interpolated);
  }
  rs->multimesh_set_physics_interpolation_quality(
      multimesh, (RenderingServer::MultimeshPhysicsInterpolationQuality)quality);
}

void InstancedRenderer3D::flush() {
  flush_queued = false;
  int count = (int)render_ids.size();
  if (!full_dirty && dirty_slots.empty() && reset_slots.empty() &&
      uploaded_count == count) {
    return;
  }
  RenderingServer *rs = RenderingServer::get_singleton();
  bool reallocated = allocated != capacity;
  if (reallocated) {
    rs->multimesh_allocate_data(multimesh, capacity,
                                RenderingServer::MULTIMESH_TRANSFORM_3D);
    allocated = capacity;
    full_dirty = true;
    uploaded_count = -1; // a fresh allocation draws all slots until set
    if (has_lods()) {
      // Creates Godot's CPU copy so LOD swaps don't read back from the GPU.
      // Bypasses the interpolator, which would swallow the write.
      if (interpolated) {
        rs->multimesh_set_physics_interpolated(multimesh, false);
      }
      rs->multimesh_instance_set_transform(multimesh, 0, Transform3D());
      if (interpolated) {
        rs->multimesh_set_physics_interpolated(multimesh, true);
      }
    }
  }

  AABB mesh_aabb = lods_aabb();
  size_t partial_max = interpolated
                           ? (size_t)(count / PARTIAL_INTERPOLATED_DIVISOR)
                           : PARTIAL_MAX_DIRTY;
  if (full_dirty || dirty_slots.size() > partial_max) {
    if (interpolated && reallocated) {
      // Fresh allocation has a zeroed previous buffer; don't lerp from it.
      rs->multimesh_set_buffer_interpolated(multimesh, buffer, buffer);
    } else {
      rs->multimesh_set_buffer(multimesh, buffer);
    }
    bounds = compute_bounds(count, mesh_aabb);
  } else {
    // Bounds only grow here; the next full upload shrinks them.
    for (int i : dirty_slots) {
      if (i >= count) {
        continue; // removed since marked
      }
      Transform3D t = read_transform(i);
      rs->multimesh_instance_set_transform(multimesh, i, t);
      AABB b = t.xform(mesh_aabb);
      bounds = bounds == AABB() ? b : bounds.merge(b);
    }
  }
  for (int i : dirty_slots) {
    slot_dirty[i] = 0;
  }
  dirty_slots.clear();

  if (interpolated) {
    for (int i : reset_slots) {
      if (i < count) {
        rs->multimesh_instance_reset_physics_interpolation(multimesh, i);
      }
    }
  }
  reset_slots.clear();

  // Stops the server recomputing bounds on every change; empty means "compute".
  rs->multimesh_set_custom_aabb(multimesh, count > 0 ? bounds : AABB());
  if (uploaded_count != count) {
    rs->multimesh_set_visible_instances(multimesh, count);
    uploaded_count = count;
  }
  full_dirty = false;
}

// Origins' box padded by mesh radius * max scale. Plain ternaries on purpose:
// MSVC doesn't inline std::min/max({...}) here.
AABB InstancedRenderer3D::compute_bounds(int count,
                                         const AABB &mesh_aabb) const {
  if (count == 0) {
    return AABB();
  }
  const float *r = buffer.ptr();
  float lo[3] = {r[3], r[7], r[11]};
  float hi[3] = {r[3], r[7], r[11]};
  float max_scale_sq = 0;
  for (int i = 0; i < count; i++, r += FLOATS_PER_INSTANCE) {
    float x = r[3], y = r[7], z = r[11];
    lo[0] = x < lo[0] ? x : lo[0];
    hi[0] = x > hi[0] ? x : hi[0];
    lo[1] = y < lo[1] ? y : lo[1];
    hi[1] = y > hi[1] ? y : hi[1];
    lo[2] = z < lo[2] ? z : lo[2];
    hi[2] = z > hi[2] ? z : hi[2];
    // Squared basis column lengths = squared scale per axis.
    float c0 = r[0] * r[0] + r[4] * r[4] + r[8] * r[8];
    float c1 = r[1] * r[1] + r[5] * r[5] + r[9] * r[9];
    float c2 = r[2] * r[2] + r[6] * r[6] + r[10] * r[10];
    float c = c0 > c1 ? c0 : c1;
    c = c > c2 ? c : c2;
    max_scale_sq = c > max_scale_sq ? c : max_scale_sq;
  }
  Vector3 far = mesh_aabb.position.abs().max(mesh_aabb.get_end().abs());
  real_t radius = far.length() * std::sqrt(max_scale_sq);
  Vector3 pad(radius, radius, radius);
  Vector3 vlo(lo[0], lo[1], lo[2]);
  Vector3 vhi(hi[0], hi[1], hi[2]);
  return AABB(vlo - pad, vhi - vlo + pad * 2);
}

AABB InstancedRenderer3D::lods_aabb() const {
  AABB box;
  for (const auto &[distance, lod_mesh] : lods) {
    if (lod_mesh.is_valid()) {
      box = box == AABB() ? lod_mesh->get_aabb() : box.merge(lod_mesh->get_aabb());
    }
  }
  return box;
}

void InstancedRenderer3D::update_lod(const Vector3 &camera_position) {
  if (render_ids.empty()) {
    return;
  }
  real_t distance = camera_position.distance_to(bounds.get_center());
  int level = 0;
  while (level + 1 < (int)lods.size() && lods[level + 1].first <= distance) {
    level++;
  }
  if (level != current_lod) {
    current_lod = level;
    RenderingServer::get_singleton()->multimesh_set_mesh(
        multimesh, lods[level].second->get_rid());
  }
}

void InstancedRenderer3D::queue_flush() {
  if (!flush_queued) {
    flush_queued = true;
    InstanceManager::get_singleton()->queue_flush(this);
  }
}

void InstancedRenderer3D::mark_dirty(int render_index, bool reset) {
  queue_flush();
  if (!slot_dirty[render_index]) {
    slot_dirty[render_index] = 1;
    dirty_slots.push_back(render_index);
  }
  if (reset) {
    reset_slots.push_back(render_index);
  }
}

void InstancedRenderer3D::write_transform(int render_index,
                                          const Transform3D &t) {
  float *w = buffer.ptrw() + render_index * FLOATS_PER_INSTANCE;
  for (int r = 0; r < 3; r++) {
    w[r * 4 + 0] = t.basis.rows[r][0];
    w[r * 4 + 1] = t.basis.rows[r][1];
    w[r * 4 + 2] = t.basis.rows[r][2];
    w[r * 4 + 3] = t.origin[r];
  }
}

Transform3D InstancedRenderer3D::read_transform(int render_index) const {
  const float *r = buffer.ptr() + render_index * FLOATS_PER_INSTANCE;
  return Transform3D(r[0], r[1], r[2], r[4], r[5], r[6], r[8], r[9], r[10],
                     r[3], r[7], r[11]);
}

int InstancedRenderer3D::slot_of(int64_t id) const {
  int64_t slot = id & 0xFFFFFFFF;
  uint32_t generation = (uint32_t)(id >> 32);
  if (id < 0 || slot >= (int64_t)records.size() || !records[slot].active ||
      records[slot].generation != generation) {
    return -1;
  }
  return (int)slot;
}

bool InstancedRenderer3D::is_instance_valid(int64_t id) const {
  return slot_of(id) >= 0;
}

int64_t InstancedRenderer3D::add_instance(const Transform3D &transform) {
  int slot;
  if (!free_ids.empty()) {
    slot = free_ids.back();
    free_ids.pop_back();
  } else {
    slot = (int)records.size();
    records.emplace_back();
  }

  int render_index = (int)render_ids.size();
  if (render_index >= capacity) {
    set_estimated_instance_count(std::max(16, capacity * 2));
  }
  render_ids.push_back(slot);
  records[slot].render_index = render_index;
  records[slot].active = true;
  write_transform(render_index, transform);
  mark_dirty(render_index, true);
  return ((int64_t)records[slot].generation << 32) | slot;
}

void InstancedRenderer3D::update_instance(int64_t id,
                                          const Transform3D &transform) {
  int slot = slot_of(id);
  if (slot < 0) {
    print_error("update_instance: invalid or stale instance id ", id);
    return;
  }
  int render_index = records[slot].render_index;
  write_transform(render_index, transform);
  mark_dirty(render_index, false);
}

void InstancedRenderer3D::update_instances(
    const PackedInt64Array &ids, const TypedArray<Transform3D> &transforms) {
  ERR_FAIL_COND_MSG(transforms.size() != ids.size(),
                    "update_instances: need one transform per id.");
  const int64_t *id = ids.ptr();
  int invalid = 0;
  for (int64_t i = 0; i < ids.size(); i++) {
    int slot = slot_of(id[i]);
    if (slot < 0) {
      invalid++;
      continue;
    }
    int render_index = records[slot].render_index;
    write_transform(render_index, transforms[i]);
    mark_dirty(render_index, false);
  }
  if (invalid > 0) {
    print_error("update_instances: skipped invalid or stale ids: ", invalid);
  }
}

void InstancedRenderer3D::remove_instance(int64_t id) {
  int slot = slot_of(id);
  if (slot < 0) {
    print_error("remove_instance: invalid or stale instance id ", id);
    return;
  }
  queue_flush();
  // Swap-remove keeps the render buffer dense.
  int index = records[slot].render_index;
  int last = (int)render_ids.size() - 1;
  if (index != last) {
    float *w = buffer.ptrw();
    std::copy_n(w + last * FLOATS_PER_INSTANCE, FLOATS_PER_INSTANCE,
                w + index * FLOATS_PER_INSTANCE);
    render_ids[index] = render_ids[last];
    records[render_ids[index]].render_index = index;
    // Otherwise the moved instance would interpolate from the removed one.
    mark_dirty(index, true);
  }
  render_ids.pop_back();
  records[slot].active = false;
  records[slot].render_index = -1;
  records[slot].generation++;
  free_ids.push_back(slot);
}

void InstancedRenderer3D::reset_instance_interpolation(int64_t id) {
  int slot = slot_of(id);
  if (slot >= 0) {
    reset_slots.push_back(records[slot].render_index);
    queue_flush();
  }
}

int InstancedRenderer3D::get_instance_count() const {
  return (int)render_ids.size();
}

void InstancedRenderer3D::clear_instances() {
  queue_flush();
  for (int slot : render_ids) {
    records[slot].active = false;
    records[slot].render_index = -1;
    records[slot].generation++; // invalidates outstanding ids
    free_ids.push_back(slot);
  }
  render_ids.clear();
}

void InstancedRenderer3D::set_estimated_instance_count(int count) {
  if (count <= capacity) {
    return;
  }
  capacity = count;
  queue_flush();
  buffer.resize(capacity * FLOATS_PER_INSTANCE);
  slot_dirty.resize(capacity, 0);
  records.reserve(capacity);
  render_ids.reserve(capacity);
}

} // namespace godot
