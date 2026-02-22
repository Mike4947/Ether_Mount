#pragma once

#include <string>

namespace EtherMount {

/// Registers or updates the Shell Namespace Extension (EtherMount VPS folder under This PC).
/// Call when saving credentials to update the display name.
class ShellExtRegistrar {
public:
    /// Register the shellext DLL. DLL must be next to EtherMount.exe.
    /// Returns true if registration succeeded.
    static bool registerShellExt(const std::string& displayName);

    /// Unregister the shellext.
    static bool unregisterShellExt();

    /// Update only the display name in the registry (shellext must already be registered).
    static bool updateDisplayName(const std::string& displayName);
};

}  // namespace EtherMount
