#pragma once

#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/mesh.hpp"
#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/variant/color.hpp"
#include "godot_cpp/variant/rect2.hpp"
#include "godot_cpp/variant/vector2.hpp"

namespace godot {

// What to draw and how to batch it. Everything sharing one definition batches.
class InstanceDefinition2D : public Resource {
  GDCLASS(InstanceDefinition2D, Resource)

  Ref<Texture2D> texture;
  Ref<Mesh> mesh; // empty = sprite
  Ref<Material> material;
  Vector2 offset; // sprites only
  Color modulate = Color(1, 1, 1, 1);
  bool y_sorted = false;
  int z_index = 0;
  real_t chunk_size = 512.0;
  real_t pixels_per_meter = 1.0; // meshes only
  int texture_filter = 0; // RenderingServer::CanvasItemTextureFilter
  int texture_repeat = 0; // RenderingServer::CanvasItemTextureRepeat

  void on_resource_changed();
  void watch(const Ref<Resource> &p_old, const Ref<Resource> &p_new);

protected:
  static void _bind_methods();

public:
  InstanceDefinition2D();

  void set_texture(const Ref<Texture2D> &p_texture);
  Ref<Texture2D> get_texture() const { return texture; }
  void set_mesh(const Ref<Mesh> &p_mesh);
  Ref<Mesh> get_mesh() const { return mesh; }
  void set_material(const Ref<Material> &p_material);
  Ref<Material> get_material() const { return material; }
  void set_offset(const Vector2 &p_offset);
  Vector2 get_offset() const { return offset; }
  void set_modulate(const Color &p_modulate);
  Color get_modulate() const { return modulate; }
  void set_y_sorted(bool p_y_sorted);
  bool is_y_sorted() const { return y_sorted; }
  void set_z_index(int p_z_index);
  int get_z_index() const { return z_index; }
  void set_chunk_size(real_t p_size);
  real_t get_chunk_size() const { return chunk_size; }
  void set_pixels_per_meter(real_t p_pixels);
  real_t get_pixels_per_meter() const { return pixels_per_meter; }
  void set_texture_filter(int p_filter);
  int get_texture_filter() const { return texture_filter; }
  void set_texture_repeat(int p_repeat);
  int get_texture_repeat() const { return texture_repeat; }
  // Scale between a mesh's own units and the canvas; 1 for sprites.
  real_t get_mesh_scale() const { return mesh.is_valid() ? pixels_per_meter : 1.0; }

  // Space one instance covers around its origin.
  Rect2 get_local_rect() const;
};

} // namespace godot
