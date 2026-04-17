#include "platform/IApplication.h"

int main()
{
    // Instantiate platform-specific application via factory function
    std::unique_ptr<west::IApplication> app = west::CreateApplication();

    if (!app->Initialize())
    {
        return 1;
    }

    app->Run();
    app->Shutdown();
    return 0;
}