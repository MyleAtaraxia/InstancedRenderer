#pragma once

#include "godot_cpp/classes/editor_export_platform.hpp"
#include "godot_cpp/classes/editor_export_plugin.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/variant/array.hpp"

namespace godot {

// What an export leaves behind for a parent's static instances: it spawns them
// under the parent, then frees itself.
class InstancedBake2D : public Node {
  GDCLASS(InstancedBake2D, Node)

  Array baked; // definition, then its transforms as 3 Vector2 columns each

protected:
  static void _bind_methods();
  void _notification(int p_what);

public:
  // Replaces the scene's plain static nodes with bakes; returns how many.
  static int bake_scene(Node *p_scene);
  void set_baked(const Array &p_baked) { baked = p_baked; }
  Array get_baked() const { return baked; }
};

class InstancedBakeExportPlugin : public EditorExportPlugin {
  GDCLASS(InstancedBakeExportPlugin, EditorExportPlugin)

protected:
  static void _bind_methods() {}

public:
  String _get_name() const override { return "InstancedBake2D"; }
  bool _begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform,
                               const PackedStringArray &p_features) const override;
  uint64_t _get_customization_configuration_hash() const override { return 2; }
  Node *_customize_scene(Node *p_scene, const String &p_path) override;
};

} // namespace godot
