#include "platform/IApplication.h"

int main()
{
    west::ApplicationDesc desc{};
    desc.windowTitle = "WestEngine";
    desc.scene.kind = west::ApplicationSceneKind::Bistro;
    desc.scene.name = "bistro";
    desc.scene.uniformScale = 0.01f;

    //desc.scene.kind = west::ApplicationSceneKind::StaticScene;
    //desc.scene.path = "assets/models/CanonicalStaticScene/CanonicalStaticScene.gltf";
    //desc.scene.name = "canonical";
    //desc.scene.uniformScale = 1.0f;

    std::unique_ptr<west::IApplication> app = west::CreateApplication(desc);

    if (!app->Initialize())
    {
        return 1;
    }

    app->Run();
    app->Shutdown();
    return 0;
}
