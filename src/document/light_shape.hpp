#pragma once
#include "canvas/face.hpp"
#include "light.hpp"

namespace dune3d {

// What a light looks like in the 3D view: a bulb, a spotlight lamp with a lens
// or a sun with rays, as small shaded solids ("size" is about its radius in mm).
// The spotlight's cone of light is drawn as lines by the renderer.
face::Faces make_light_shape(const Light &light, double size);

// The light itself as see-through solids in the light's colour: a glow around
// a lamp, the cone of a spotlight (as wide as the beam is at the target), the
// parallel rays of the sun. Drawn with ICanvas::face_material_beam.
face::Faces make_light_beam(const Light &light, double size);

// How big lights are drawn, from how far they are from what they're aimed at
double get_light_shape_size(const Light &light);

} // namespace dune3d
