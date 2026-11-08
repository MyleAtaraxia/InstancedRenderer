#include "instanced_renderer_3d.h"
#include "godot_cpp/classes/rendering_server.hpp"
#include "instance_manager_3d.h"
#include <algorithm>
#include <cmath>

namespace godot {

static constexpr size_t PARTIAL_MAX_DIRTY = 16;
static constexpr int PARTIAL_INTERPOLATED_DIVISOR = 32;

InstancedRenderer3D::InstancedRenderer3D(
    const Ref<World3D> &p_world, const Ref<InstanceDefinition3D> &p_definition,
    const Vector3i &p_cell, bool p_frozen, int initial_capacity)
    : world(p_world), definition(p_definition), cell(p_cell), frozen(p_frozen),
      lods(p_definition->get_lod_levels()) {
  real_t size = definition->get_chunk_size();
  bounded = size > 0;
  Vector3 margin = Vector3(1, 1, 1) * (size * CELL_MARGIN);
  cell_lo = Vector3(cell) * size - margin;
  cell_hi = Vector3(cell + Vector3i(1, 1, 1)) * size + margin;
  RenderingServer *rs = RenderingServer::get_singleton();
  multimesh = rs->multimesh_create();
  instance = rs->instance_create();
  rs->instance_set_base(instance, multimesh);
  rs->multimesh_set_mesh(multimesh, lods[0].second.is_valid()
                                        ? lods[0].second->get_rid()
                                        : RID());
  Ref<Material> material = definition->get_material();
  rs->instance_geometry_set_material_override(
      instance, material.is_valid() ? material->get_rid() : RID());
  rs->instance_geometry_set_cast_shadows_setting(
      instance,
      (RenderingServer::ShadowCastingSetting)definition->get_cast_shadow());
  rs->instance_set_layer_mask(instance, definition->get_layers());
  // Godot measures this from the batch's bounds centre, not per instance.
  rs->instance_geometry_set_visibility_range(
      instance, 0, std::max<real_t>(definition->get_visibility_range_end(), 0),
      0, 0, RenderingServer::VISIBILITY_RANGE_FADE_DISABLED);
  rs->instance_set_scenario(instance, world->get_scenario());
  reserve(initial_capacity);
}

InstancedRenderer3D::~InstancedRenderer3D() {
  RenderingServer *rs = RenderingServer::get_singleton();
  rs->free_rid(instance);
  rs->free_rid(multimesh);
}

void InstancedRenderer3D::set_interpolation(bool p_interpolated, int quality) {
  p_interpolated = p_interpolated && !frozen;
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
  int count = get_instance_count();
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
  if (owners.empty()) {
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
    InstanceManager3D::get_singleton()->queue_flush(this);
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

int InstancedRenderer3D::add(int owner, const Transform3D &t) {
  int render_index = get_instance_count();
  if (render_index >= capacity) {
    reserve(std::max(16, capacity * 2));
  }
  owners.push_back(owner);
  write_transform(render_index, t);
  mark_dirty(render_index, true);
  return render_index;
}

void InstancedRenderer3D::set_transform(int render_index, const Transform3D &t) {
  write_transform(render_index, t);
  mark_dirty(render_index, false);
}

void InstancedRenderer3D::set_origin(int render_index, const Vector3 &origin) {
  float *w = buffer.ptrw() + render_index * FLOATS_PER_INSTANCE;
  w[3] = origin.x;
  w[7] = origin.y;
  w[11] = origin.z;
  mark_dirty(render_index, false);
}

void InstancedRenderer3D::set_packed(int render_index, const float *src) {
  std::copy_n(src, FLOATS_PER_INSTANCE,
              buffer.ptrw() + render_index * FLOATS_PER_INSTANCE);
  mark_dirty(render_index, false);
}

int InstancedRenderer3D::remove(int render_index) {
  queue_flush();
  int last = get_instance_count() - 1;
  int moved = -1;
  // Swap-remove keeps the render buffer dense.
  if (render_index != last) {
    float *w = buffer.ptrw();
    std::copy_n(w + last * FLOATS_PER_INSTANCE, FLOATS_PER_INSTANCE,
                w + render_index * FLOATS_PER_INSTANCE);
    moved = owners[last];
    owners[render_index] = moved;
    // Otherwise the moved instance would interpolate from the removed one.
    mark_dirty(render_index, true);
  }
  owners.pop_back();
  return moved;
}

void InstancedRenderer3D::reset_interpolation(int render_index) {
  reset_slots.push_back(render_index);
  queue_flush();
}

void InstancedRenderer3D::reserve(int count) {
  if (count <= capacity) {
    return;
  }
  capacity = count;
  queue_flush();
  buffer.resize(capacity * FLOATS_PER_INSTANCE);
  slot_dirty.resize(capacity, 0);
  owners.reserve(capacity);
}

} // namespace godot
