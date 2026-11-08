#pragma once

#include "godot_cpp/classes/material.hpp"
#include "godot_cpp/classes/mesh.hpp"
#include "godot_cpp/classes/resource.hpp"
#include "godot_cpp/variant/typed_array.hpp"
#include "instanced_mesh_lod.h"
#include <utility>
#include <vector>

namespace godot {

// (start distance, mesh), sorted; level 0 is the base mesh at distance 0.
using LodLevels = std::vector<std::pair<real_t, Ref<Mesh>>>;

// What to draw and how to batch it. Everything sharing one definition batches.
class InstanceDefinition3D : public Resource {
  GDCLASS(InstanceDefinition3D, Resource)

  Ref<Mesh> mesh;
  Ref<Material> material;
  TypedArray<InstancedMeshLOD> lods;
  int cast_shadow = 1; // RenderingServer::ShadowCastingSetting
  uint32_t layers = 1;
  real_t visibility_range_end = 0; // 0 = always drawn
  real_t chunk_size = 64.0;

  void on_resource_changed();
  void watch(const Ref<Resource> &p_old, const Ref<Resource> &p_new);

protected:
  static void _bind_methods();

public:
  InstanceDefinition3D();

  void set_mesh(const Ref<Mesh> &p_mesh);
  Ref<Mesh> get_mesh() const { return mesh; }
  void set_material(const Ref<Material> &p_material);
  Ref<Material> get_material() const { return material; }
  void set_lods(const TypedArray<InstancedMeshLOD> &p_lods);
  TypedArray<InstancedMeshLOD> get_lods() const { return lods; }
  void set_cast_shadow(int p_setting);
  int get_cast_shadow() const { return cast_shadow; }
  void set_layers(uint32_t p_layers);
  uint32_t get_layers() const { return layers; }
  void set_visibility_range_end(real_t p_distance);
  real_t get_visibility_range_end() const { return visibility_range_end; }
  void set_chunk_size(real_t p_size);
  real_t get_chunk_size() const { return chunk_size; }

  // Sorted, without levels at or past visibility_range_end, base mesh first.
  LodLevels get_lod_levels() const;
};

} // namespace godot
