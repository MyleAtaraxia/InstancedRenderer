#pragma once

#include "godot_cpp/classes/editor_export_platform.hpp"
#include "godot_cpp/classes/editor_export_plugin.hpp"
#include "godot_cpp/classes/editor_file_system_directory.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/script.hpp"
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
  static int bake_scene(Node *p_scene, const Ref<Script> &p_progress = Ref<Script>(),
                        double p_from = 0.0, double p_to = 1.0);
  // The GDProgress script; it needs the editor, so games never load it.
  static Ref<Script> progress_script();
  static void set_progress_task(bool p_added);
  void set_baked(const Array &p_baked) { baked = p_baked; }
  Array get_baked() const { return baked; }
};

class InstancedBakeExportPlugin : public EditorExportPlugin {
  GDCLASS(InstancedBakeExportPlugin, EditorExportPlugin)

  Ref<Script> progress;
  uint64_t scene_bytes = 0; // scene files stand in for the work, which is unknown until they load
  uint64_t scene_bytes_done = 0;

  void add_scene_bytes(EditorFileSystemDirectory *p_directory);
  void end_progress();

protected:
  static void _bind_methods() {}

public:
  String _get_name() const override { return "InstancedBake2D"; }
  bool _begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform,
                               const PackedStringArray &p_features) const override;
  uint64_t _get_customization_configuration_hash() const override { return 2; }
  Node *_customize_scene(Node *p_scene, const String &p_path) override;
  void _export_begin(const PackedStringArray &p_features, bool p_is_debug,
                     const String &p_path, uint32_t p_flags) override;
  void _end_customize_scenes() override { end_progress(); }
};

} // namespace godot
