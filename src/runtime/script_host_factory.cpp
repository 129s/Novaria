#include "runtime/script_host_factory.h"

#include "script/lua_jit_script_host.h"

namespace novaria::runtime {

std::unique_ptr<script::IScriptHost> CreateScriptHost() {
    return std::make_unique<script::LuaJitScriptHost>();
}

}  // namespace novaria::runtime

