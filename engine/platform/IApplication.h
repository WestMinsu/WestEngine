// =============================================================================
// WestEngine - Platform
// Application lifecycle interface
// =============================================================================
#pragma once

#include <memory>

namespace west
{

/// Abstract application lifecycle.
/// Implementations: Win32Application, future: LinuxApplication, etc.
class IApplication
{
public:
    virtual ~IApplication() = default;

    /// Initialize the application (create window, subsystems).
    /// @return true on success, false on fatal error.
    [[nodiscard]] virtual bool Initialize() = 0;

    /// Run the main loop. Returns when the application should exit.
    virtual void Run() = 0;

    /// Clean up all resources.
    virtual void Shutdown() = 0;
};

/// External factory function to be implemented by the platform backend.
/// This allows main.cpp to remain platform-agnostic.
std::unique_ptr<IApplication> CreateApplication();

} // namespace west
