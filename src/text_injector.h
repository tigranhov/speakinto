#pragma once
#include <string>

namespace injector {

// Inject text into the currently focused input:
// 1. Save clipboard
// 2. Set clipboard to text
// 3. SendInput Ctrl+V
// 4. Restore clipboard
void injectText(const std::string& text);

}
