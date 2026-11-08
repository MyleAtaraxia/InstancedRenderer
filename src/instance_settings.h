#pragma once

#include "godot_cpp/core/property_info.hpp"
#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/variant/variant.hpp"

namespace godot {

// Project settings shared by the 2D and 3D managers, reloaded live.
namespace instance_settings {

// No "always": the engine only ticks interpolation when the project enables it.
enum InterpolationMode {
  INTERPOLATION_FOLLOW_PROJECT,
  INTERPOLATION_DISABLED,
};

constexpr const char *INTERPOLATION_MODE = "instanced_renderer/interpolation/mode";
constexpr const char *INTERPOLATION_QUALITY =
    "instanced_renderer/interpolation/quality";
constexpr const char *INITIAL_CAPACITY = "instanced_renderer/buffer/initial_capacity";
constexpr const char *SLEEP_AFTER_FRAMES =
    "instanced_renderer/batching/sleep_after_frames";
constexpr const char *GIZMO_COLOR = "instanced_renderer/editor/gizmo_color";

void add_setting(const String &name, const Variant &default_value,
                 PropertyHint hint = PROPERTY_HINT_NONE,
                 const String &hint_string = "");
void register_shared();
// False in the editor, when disabled, or when the project doesn't interpolate.
bool should_interpolate(int interpolation_mode);

} // namespace instance_settings

} // namespace godot
