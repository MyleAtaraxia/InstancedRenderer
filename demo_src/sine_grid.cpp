#include "sine_grid.h"
#include <cmath>
#include <godot_cpp/core/class_db.hpp>

namespace godot {

void SineGrid::_bind_methods() {
  ClassDB::bind_method(D_METHOD("configure", "params"), &SineGrid::configure);
  ClassDB::bind_method(D_METHOD("step", "time"), &SineGrid::step);
  ClassDB::bind_method(D_METHOD("get_count"), &SineGrid::get_count);
}

void SineGrid::configure(const Dictionary &params) {
  size_x = params.get("size_x", size_x);
  size_z = params.get("size_z", size_z);
  spacing = params.get("spacing", spacing);
  amplitude = params.get("amplitude", amplitude);
  frequency = params.get("frequency", frequency);
  speed = params.get("speed", speed);
  transforms.resize(get_count());
}

TypedArray<Transform3D> SineGrid::step(double time) {
  int i = 0;
  real_t phase = speed * time;
  real_t x0 = -(size_x - 1) * spacing * 0.5;
  real_t z0 = -(size_z - 1) * spacing * 0.5;
  for (int z = 0; z < size_z; z++) {
    real_t pz = z0 + z * spacing;
    real_t wave_z = std::sin(frequency * pz + phase);
    for (int x = 0; x < size_x; x++, i++) {
      real_t px = x0 + x * spacing;
      real_t y = amplitude * std::sin(frequency * px + phase) * wave_z;
      transforms[i] = Transform3D(Basis(), Vector3(px, y, pz));
    }
  }
  return transforms;
}

} // namespace godot
