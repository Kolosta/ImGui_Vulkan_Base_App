#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ModuleAPI.h"

namespace App::Modules {

// ─────────────────────────────────────────────────────────────────────────────
//  ModuleRegistry — the catalogue of available modules.
//
//  Internal modules (Typography, IOF Mapping…) are constructed in
//  RegisterInternal(). Each registered module gets OnRegister() called once so it
//  can add its editors to the EditorRegistry. The splash lists All(); the app
//  activates one by id. External (plugin) modules will be added later by a DLL
//  loader that calls Add() with the IModule* from CartoCreateModule().
// ─────────────────────────────────────────────────────────────────────────────
class ModuleRegistry {
public:
    static ModuleRegistry& Instance();

    // Construct + register all built-in modules and call their OnRegister(ctx).
    // Idempotent: safe to call once at startup.
    void RegisterInternal(ModuleContext& ctx);

    // Take ownership of a module (internal or, later, from a plugin DLL) and run
    // its OnRegister(ctx). Ignored if a module with the same id already exists.
    void Add(std::unique_ptr<IModule> mod, ModuleContext& ctx);

    IModule* Get(const std::string& id) const;
    const std::vector<std::unique_ptr<IModule>>& All() const { return modules_; }

    // TODO(plugins): scan <exe>/modules/*.dll, dlopen/LoadLibrary, verify
    // CartoModuleAbiVersion() == kModuleAbiVersion, then Add(CartoCreateModule()).
    // Designed for but intentionally not implemented in this first pass.
    void LoadExternalModules(ModuleContext& ctx);

private:
    ModuleRegistry() = default;
    bool registered_ = false;
    std::vector<std::unique_ptr<IModule>> modules_;
};

}  // namespace App::Modules
