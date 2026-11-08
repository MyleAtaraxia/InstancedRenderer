#include "instance_settings.h"
#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/classes/scene_tree.hpp"
#include "godot_cpp/variant/color.hpp"
#include "godot_cpp/variant/dictionary.hpp"

namespace godot {
namespace instance_settings {

void add_setting(const String &name, const Variant &default_value,
                 PropertyHint hint, const String &hint_string) {
  ProjectSettings *ps = ProjectSettings::get_singleton();
  if (!ps->has_setting(name)) {
    ps->set_setting(name, default_value);
  }
  ps->set_initial_value(name, default_value);
  ps->set_as_basic(name, true);
  Dictionary info;
  info["name"] = name;
  info["type"] = default_value.get_type();
  info["hint"] = hint;
  info["hint_string"] = hint_string;
  ps->add_property_info(info);
}

void register_shared() {
  add_setting(INTERPOLATION_MODE, INTERPOLATION_FOLLOW_PROJECT,
              PROPERTY_HINT_ENUM, "Follow Project,Disabled");
  add_setting(INTERPOLATION_QUALITY, 0, PROPERTY_HINT_ENUM, "Fast,High");
  add_setting(INITIAL_CAPACITY, 16, PROPERTY_HINT_RANGE, "1,65536,1,or_greater");
  add_setting(SLEEP_AFTER_FRAMES, 60, PROPERTY_HINT_RANGE, "0,600,1,or_greater");
  add_setting(GIZMO_COLOR, Color(1.0, 0.8, 0.2));
}

bool should_interpolate(int interpolation_mode) {
  if (Engine::get_singleton()->is_editor_hint()) {
    return false; // the editor never runs physics interpolation
  }
  if (interpolation_mode == INTERPOLATION_DISABLED) {
    return false;
  }
  SceneTree *tree =
      Object::cast_to<SceneTree>(Engine::get_singleton()->get_main_loop());
  return tree && tree->is_physics_interpolation_enabled();
}

} // namespace instance_settings
} // namespace godot
