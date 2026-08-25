#include "n64/frontend/application.hpp"

int main(int argc, char** argv) {
    n64::frontend::Application app;
    const auto cfg = n64::frontend::Application::parse_args(argc, argv);
    return app.run(cfg);
}
