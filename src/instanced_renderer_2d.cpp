#include "instanced_renderer_2d.h"
#include "godot_cpp/classes/rendering_server.hpp"
#include "instance_manager_2d.h"
#include <algorithm>
#include <cmath>

namespace godot {

static constexpr size_t PARTIAL_MAX_DIRTY = 16;
static constexpr int PARTIAL_INTERPOLATED_DIVISOR = 32;

static RID texture_rid(const Ref<InstanceDefinition2D> &definition) {
  Ref<Texture2D> texture = definition->get_texture();
  return texture.is_valid() ? texture->get_rid() : RID();
}

static void apply_texture_settings(RID item,
                                   const Ref<InstanceDefinition2D> &definition) {
  RenderingServer *rs = RenderingServer::get_singleton();
  rs->canvas_item_set_default_texture_filter(
      item, (RenderingServer::CanvasItemTextureFilter)definition->get_texture_filter());
  rs->canvas_item_set_default_texture_repeat(
      item, (RenderingServer::CanvasItemTextureRepeat)definition->get_texture_repeat());
}

static RID material_rid(const Ref<InstanceDefinition2D> &definition) {
  Ref<Material> material = definition->get_material();
  return material.is_valid() ? material->get_rid() : RID();
}

InstancedRenderer2D::InstancedRenderer2D(
    InstanceParent2D *p_parent, const Ref<InstanceDefinition2D> &p_definition,
    const Vector2i &p_cell, bool p_frozen, int initial_capacity)
    : parent(p_parent), definition(p_definition), cell(p_cell),
      y_sorted(p_definition->is_y_sorted()), frozen(p_frozen),
      local_rect(p_definition->get_local_rect()),
      mesh_scale(p_definition->get_mesh_scale()) {
  parent->renderer_count++;
  if (y_sorted) {
    parent->y_sorted.push_back(this);
    return;
  }
  RenderingServer *rs = RenderingServer::get_singleton();
  item = rs->canvas_item_create();
  multimesh = rs->multimesh_create();
  Ref<Mesh> mesh = definition->get_mesh();
  uses_quad = mesh.is_null();
  rs->multimesh_set_mesh(multimesh, uses_quad
                                        ? InstanceManager2D::get_singleton()
                                              ->acquire_quad(local_rect)
                                        : mesh->get_rid());
  rs->canvas_item_set_parent(item, parent->item);
  rs->canvas_item_set_z_index(item, definition->get_z_index());
  rs->canvas_item_set_self_modulate(item, definition->get_modulate());
  rs->canvas_item_set_material(item, material_rid(definition));
  apply_texture_settings(item, definition);
  rs->canvas_item_add_multimesh(item, multimesh, texture_rid(definition));
  reserve(initial_capacity);
}

InstancedRenderer2D::~InstancedRenderer2D() {
  parent->renderer_count--;
  InstanceManager2D *manager = InstanceManager2D::get_singleton();
  if (y_sorted) {
    for (const RID &rid : items) {
      manager->release_item(rid);
    }
    parent->y_sorted.erase(
        std::remove(parent->y_sorted.begin(), parent->y_sorted.end(), this),
        parent->y_sorted.end());
    return;
  }
  RenderingServer *rs = RenderingServer::get_singleton();
  rs->free_rid(item);
  rs->free_rid(multimesh);
  if (uses_quad) {
    manager->release_quad(local_rect);
  }
}

void InstancedRenderer2D::set_interpolation(bool p_interpolated) {
  p_interpolated = p_interpolated && !frozen;
  if (p_interpolated == interpolated) {
    return;
  }
  interpolated = p_interpolated;
  RenderingServer *rs = RenderingServer::get_singleton();
  if (y_sorted) {
    for (const RID &rid : items) {
      rs->canvas_item_set_interpolated(rid, interpolated);
    }
  } else {
    // Left at Fast: the High path assumes the 12-float 3D layout.
    rs->multimesh_set_physics_interpolated(multimesh, interpolated);
  }
}

void InstancedRenderer2D::set_on_screen(bool p_on_screen) {
  if (p_on_screen == on_screen) {
    return;
  }
  on_screen = p_on_screen;
  if (on_screen) {
    int count = get_instance_count();
    items.resize(count);
    for (int i = 0; i < count; i++) {
      items[i] = show_instance(i);
    }
  } else {
    InstanceManager2D *manager = InstanceManager2D::get_singleton();
    for (const RID &rid : items) {
      manager->release_item(rid);
    }
    items.clear();
  }
}

RID InstancedRenderer2D::show_instance(int render_index) {
  RenderingServer *rs = RenderingServer::get_singleton();
  RID ci = InstanceManager2D::get_singleton()->acquire_item();
  rs->canvas_item_set_parent(ci, parent->sort_root);
  rs->canvas_item_set_interpolated(ci, interpolated);
  rs->canvas_item_set_transform(ci, read_transform(render_index));
  if (interpolated) {
    rs->canvas_item_reset_physics_interpolation(ci);
  }
  rs->canvas_item_set_z_index(ci, definition->get_z_index());
  rs->canvas_item_set_self_modulate(ci, definition->get_modulate());
  rs->canvas_item_set_material(ci, material_rid(definition));
  apply_texture_settings(ci, definition);
  Ref<Mesh> mesh = definition->get_mesh();
  if (mesh.is_valid()) {
    rs->canvas_item_add_mesh(ci, mesh->get_rid(),
                             Transform2D().scaled(Vector2(mesh_scale, mesh_scale)),
                             Color(1, 1, 1, 1), texture_rid(definition));
  } else {
    rs->canvas_item_add_texture_rect(ci, local_rect, texture_rid(definition));
  }
  return ci;
}

void InstancedRenderer2D::flush() {
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
                                RenderingServer::MULTIMESH_TRANSFORM_2D);
    allocated = capacity;
    full_dirty = true;
    uploaded_count = -1; // a fresh allocation draws all slots until set
  }

  size_t partial_max = interpolated
                           ? (size_t)(count / PARTIAL_INTERPOLATED_DIVISOR)
                           : PARTIAL_MAX_DIRTY;
  bool full = full_dirty || dirty_slots.size() > partial_max;
  if (full) {
    if (interpolated && reallocated) {
      // Fresh allocation has a zeroed previous buffer; don't lerp from it.
      rs->multimesh_set_buffer_interpolated(multimesh, buffer, buffer);
    } else {
      rs->multimesh_set_buffer(multimesh, buffer);
    }
    bounds = compute_bounds();
  } else {
    for (int i : dirty_slots) {
      if (i < count) {
        upload_slot(i);
      }
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
  rs->multimesh_set_custom_aabb(
      multimesh, AABB(Vector3(bounds.position.x, bounds.position.y, 0),
                      Vector3(bounds.size.x, bounds.size.y, 0)));
  rs->canvas_item_set_custom_rect(item, true, bounds);
  if (uploaded_count != count) {
    // With motion vectors on, an interpolated upload can land in the half of the
    // buffer the canvas doesn't read, while the count applies at once and draws
    // old data under it. Allocating resets the count, so that case can't wait.
    InstanceManager2D *manager = InstanceManager2D::get_singleton();
    manager->forget_visible_counts(this);
    if (interpolated && !reallocated) {
      manager->queue_visible_count(this, count);
    } else {
      rs->multimesh_set_visible_instances(multimesh, count);
    }
    uploaded_count = count;
  }
  full_dirty = false;
}

void InstancedRenderer2D::upload_slot(int render_index) {
  RenderingServer::get_singleton()->multimesh_instance_set_transform_2d(
      multimesh, render_index,
      read_transform(render_index).scaled_local(Vector2(mesh_scale, mesh_scale)));
}

void InstancedRenderer2D::set_visible_count(int count) {
  RenderingServer::get_singleton()->multimesh_set_visible_instances(multimesh, count);
}

// Origins' box padded by drawable radius * max scale.
Rect2 InstancedRenderer2D::compute_bounds() const {
  int count = get_instance_count();
  if (count == 0) {
    return Rect2();
  }
  const float *r = buffer.ptr();
  float lo_x = r[3], hi_x = r[3], lo_y = r[7], hi_y = r[7];
  float max_scale_sq = 0;
  for (int i = 0; i < count; i++, r += FLOATS_PER_INSTANCE) {
    float x = r[3], y = r[7];
    lo_x = x < lo_x ? x : lo_x;
    hi_x = x > hi_x ? x : hi_x;
    lo_y = y < lo_y ? y : lo_y;
    hi_y = y > hi_y ? y : hi_y;
    // Squared basis column lengths = squared scale per axis.
    float c0 = r[0] * r[0] + r[4] * r[4];
    float c1 = r[1] * r[1] + r[5] * r[5];
    float c = c0 > c1 ? c0 : c1;
    max_scale_sq = c > max_scale_sq ? c : max_scale_sq;
  }
  Vector2 far = local_rect.position.abs().max(local_rect.get_end().abs());
  real_t radius = far.length() * std::sqrt(max_scale_sq) / mesh_scale;
  return Rect2(lo_x - radius, lo_y - radius, hi_x - lo_x + radius * 2,
               hi_y - lo_y + radius * 2);
}

// Only grows; a chunked full upload shrinks it again.
void InstancedRenderer2D::grow_bounds(const Transform2D &t) {
  Rect2 b = t.xform(local_rect);
  if (bounds == Rect2()) {
    bounds = b;
  } else if (bounds.encloses(b)) {
    return;
  } else {
    bounds = bounds.merge(b);
  }
  if (y_sorted) {
    parent->view_dirty = true;
  }
}

void InstancedRenderer2D::queue_flush() {
  if (!flush_queued) {
    flush_queued = true;
    InstanceManager2D::get_singleton()->queue_flush(this);
  }
}

void InstancedRenderer2D::mark_dirty(int render_index, bool reset) {
  queue_flush();
  if (!slot_dirty[render_index]) {
    slot_dirty[render_index] = 1;
    dirty_slots.push_back(render_index);
  }
  if (reset) {
    reset_slots.push_back(render_index);
  }
}

void InstancedRenderer2D::write_transform(int render_index,
                                          const Transform2D &t) {
  float *w = buffer.ptrw() + render_index * FLOATS_PER_INSTANCE;
  w[0] = t.columns[0][0] * mesh_scale;
  w[1] = t.columns[1][0] * mesh_scale;
  w[2] = 0;
  w[3] = t.columns[2][0];
  w[4] = t.columns[0][1] * mesh_scale;
  w[5] = t.columns[1][1] * mesh_scale;
  w[6] = 0;
  w[7] = t.columns[2][1];
}

Transform2D InstancedRenderer2D::read_transform(int render_index) const {
  const float *r = buffer.ptr() + render_index * FLOATS_PER_INSTANCE;
  return Transform2D(r[0] / mesh_scale, r[4] / mesh_scale, r[1] / mesh_scale,
                     r[5] / mesh_scale, r[3], r[7]);
}

int InstancedRenderer2D::add(int owner, const Transform2D &t) {
  int render_index = get_instance_count();
  if (render_index >= capacity) {
    reserve(std::max(16, capacity * 2));
  }
  owners.push_back(owner);
  write_transform(render_index, t);
  grow_bounds(t);
  if (!y_sorted) {
    mark_dirty(render_index, true);
  } else if (on_screen) {
    items.push_back(show_instance(render_index));
  }
  return render_index;
}

void InstancedRenderer2D::set_transform(int render_index,
                                        const Transform2D &t) {
  write_transform(render_index, t);
  grow_bounds(t);
  if (!y_sorted) {
    mark_dirty(render_index, false);
  } else if (on_screen) {
    RenderingServer::get_singleton()->canvas_item_set_transform(
        items[render_index], t);
  }
}

int InstancedRenderer2D::remove(int render_index) {
  int last = get_instance_count() - 1;
  int moved = -1;
  if (on_screen) {
    InstanceManager2D::get_singleton()->release_item(items[render_index]);
    items[render_index] = items[last];
    items.pop_back();
  }
  if (!y_sorted) {
    queue_flush();
  }
  // Swap-remove keeps the render buffer dense.
  if (render_index != last) {
    float *w = buffer.ptrw();
    std::copy_n(w + last * FLOATS_PER_INSTANCE, FLOATS_PER_INSTANCE,
                w + render_index * FLOATS_PER_INSTANCE);
    moved = owners[last];
    owners[render_index] = moved;
    if (!y_sorted) {
      // Otherwise the moved instance would interpolate from the removed one.
      mark_dirty(render_index, true);
    }
  }
  owners.pop_back();
  if (owners.empty()) {
    bounds = Rect2();
  }
  return moved;
}

void InstancedRenderer2D::reset_interpolation(int render_index) {
  if (!y_sorted) {
    reset_slots.push_back(render_index);
    queue_flush();
  } else if (on_screen) {
    RenderingServer::get_singleton()->canvas_item_reset_physics_interpolation(
        items[render_index]);
  }
}

void InstancedRenderer2D::reserve(int count) {
  if (count <= capacity) {
    return;
  }
  capacity = count;
  buffer.resize(capacity * FLOATS_PER_INSTANCE);
  owners.reserve(capacity);
  if (!y_sorted) {
    slot_dirty.resize(capacity, 0);
    queue_flush();
  }
}

} // namespace godot
