#include "ModuleRegistry.h"

#include "Typography/TypographyModule.h"
#include "IofMapping/IofMappingModule.h"

namespace App::Modules {

ModuleRegistry& ModuleRegistry::Instance() {
    static ModuleRegistry inst;
    return inst;
}

void ModuleRegistry::Add(std::unique_ptr<IModule> mod, ModuleContext& ctx) {
    if (!mod) return;
    if (Get(mod->Info().id)) return;          // dedup by id
    mod->OnRegister(ctx);                       // let it add its editors/shortcuts
    modules_.push_back(std::move(mod));
}

void ModuleRegistry::RegisterInternal(ModuleContext& ctx) {
    if (registered_) return;
    registered_ = true;
    // The set of built-in modules. Adding a new internal module is one line here
    // plus its own folder under src/Modules/.
    Add(std::make_unique<Typography::TypographyModule>(), ctx);
    Add(std::make_unique<IofMapping::IofMappingModule>(), ctx);

    // External (plugin) modules dropped into <exe>/modules/ — future.
    LoadExternalModules(ctx);
}

void ModuleRegistry::LoadExternalModules(ModuleContext& /*ctx*/) {
    // TODO(plugins): enumerate <exe>/modules/*.dll, load each, check
    // CartoModuleAbiVersion() against kModuleAbiVersion, then
    // Add(std::unique_ptr<IModule>(CartoCreateModule()), ctx). Not implemented in
    // this first pass — the contract (ModuleAPI.h) is what external authors build
    // against; see src/Modules/README.md.
}

IModule* ModuleRegistry::Get(const std::string& id) const {
    for (const auto& m : modules_)
        if (m->Info().id == id) return m.get();
    return nullptr;
}

}  // namespace App::Modules
