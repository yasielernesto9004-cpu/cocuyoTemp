#pragma once

#include "mono_runtime.hpp"

namespace overlay {

void shellui_publish_ready();
bool shellui_hooks_are_ready();
bool install_overlay_hooks(MonoImage* pui_image);
void uninstall_overlay_hooks();

} // namespace overlay
