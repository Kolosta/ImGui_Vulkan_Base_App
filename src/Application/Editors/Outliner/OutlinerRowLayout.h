#pragma once

#include <UI/Widgets/TreeRow.h>

// The Outliner's row chrome now lives in UI/Widgets/TreeRow.h so other editors
// (Colour Usage) build their hierarchies from the same primitives. `ol` stays
// as the Outliner's local spelling of it.
namespace App {
namespace ol = UI::Tree;
}  // namespace App
