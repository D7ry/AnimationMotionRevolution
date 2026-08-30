#include "Hooks.h"
#include "Settings.h"
#ifdef AMR_ENABLE_TRUEHUD_DEBUG
#include "TrueHUDIntegration.h"
#endif

#include "utils/Logger.h"

namespace
{
	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
#ifdef AMR_ENABLE_TRUEHUD_DEBUG
		if (a_message && a_message->type == SKSE::MessagingInterface::kPostLoad) {
			truehud::Initialize();
		}
#else
		(void)a_message;
#endif
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	REL::Module::reset();

	const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
	if (!logger::init(plugin->GetName())) {
		return false;
	}

	logger::info("Loading {} {}...", plugin->GetName(), plugin->GetVersion());

	SKSE::Init(a_skse);
	if (!SKSE::GetMessagingInterface()->RegisterListener("SKSE", OnSKSEMessage)) {
		logger::critical("Failed to register SKSE messaging listener");
		return false;
	}
	settings::Init("AnimationMotionRevolution.ini");
	logger::set_level(settings::debug::logLevel, settings::debug::logLevel);
	logger::info("[AMR-DIAG] diagnostic build 2026-08-30-q; runtime {}", REL::Module::get().version().string());

	hooks::Install();

	logger::info("Successfully loaded");
	return true;
}
