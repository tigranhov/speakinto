#pragma once
#include <string>
#include <functional>

namespace model {

enum class ModelSize { Tiny, Base, Small, Medium };

const char* modelSizeName(ModelSize size);
const char* modelSizeString(ModelSize size);     // for JSON: "tiny", "base", etc.
ModelSize modelSizeFromString(const std::string& s); // parse from JSON

std::wstring getModelPath(ModelSize size);
bool modelExists(ModelSize size);
bool downloadModel(ModelSize size, std::function<void(int percent)> onProgress = nullptr);
void deleteModel(ModelSize size);
void deleteAllExcept(ModelSize keep);

}
