#pragma once

#include "script/script_host.h"

#include <memory>

namespace novaria::runtime {

std::unique_ptr<script::IScriptHost> CreateScriptHost();

}  // namespace novaria::runtime

