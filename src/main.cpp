#include "app/game_app.h"

#include <filesystem>
#include <string_view>

int main(int argc, char* argv[]) {
    std::filesystem::path config_path = "config/game.toml";
    if (argc > 1) {
        config_path = argv[1];
    }

    novaria::app::GameApp app;
    if (!app.Initialize(config_path)) {
        return 1;
    }

    const int exit_code = app.Run();
    app.Shutdown();
    return exit_code;
}
