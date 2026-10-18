#pragma once
#include "util/uuid.hpp"
#include "group/group_thread.hpp"

namespace dune3d {
class Document;

// The thread step of a body (a GroupThread as its last group), or nullptr.
GroupThread *find_thread(Document &doc, const UUID &body);

// Winds a helical thread round the body's upright cylinder (see GroupThread),
// adding or updating the group. The caller rebuilds.
GroupThread *set_thread(Document &doc, const UUID &body, double root_radius, double pitch, double depth,
                        double length);

} // namespace dune3d
