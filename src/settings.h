#pragma once
#include <windows.h>
#include <string>
#include "model_manager.h"

namespace settings {

enum class RepeatPressMode { Queue, Flash, Cancel };

struct Settings {
    RepeatPressMode repeatPressMode = RepeatPressMode::Queue;
    int selectedMicIndex = -1;
    model::ModelSize modelSize = model::ModelSize::Small;
};

Settings load();
void save(const Settings& s);
bool isDialogOpen();

bool showSettingsDialog(HINSTANCE hInstance, Settings& s, const wchar_t* backendInfo = L"");

}
