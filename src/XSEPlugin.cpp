#include "ENBLightAffectsStealth.h"
#include <ENB/ENBSeriesAPI.h>

ENB_API::ENBSDKALT1002* g_ENB = nullptr;

void Init()
{
	g_ENB = reinterpret_cast<ENB_API::ENBSDKALT1002*>(ENB_API::RequestENBAPI(ENB_API::SDKVersion::V1002));
	if (g_ENB) {
		logger::info("Obtained ENB API, installing hooks");
		ENBLightAffectsStealth::InstallHooks();
		g_ENB->SetCallbackFunction([](ENBCallbackType calltype) {
			switch (calltype) {
			case ENBCallbackType::ENBCallback_BeginFrame:
				ENBLightAffectsStealth::GetSingleton()->UpdateSettings(g_ENB);
				ENBLightAffectsStealth::GetSingleton()->UpdateLights();
				break;
			case ENBCallbackType::ENBCallback_EndFrame:
				ENBLightAffectsStealth::GetSingleton()->Reset();
				break;
			}
		});
	} else {
		logger::info("Unable to acquire ENB API, not installing hooks");
	}
}

void InitializeLog()
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = spdlog::level::trace;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(level);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%v");
}

EXTERN_C [[maybe_unused]] __declspec(dllexport) bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
#ifndef NDEBUG
	while (!IsDebuggerPresent()) {};
#endif

	InitializeLog();

	logger::info(("{} v{}"), Plugin::NAME, Plugin::VERSION);
	logger::info("Game version : {}", a_skse->RuntimeVersion().string());

	SKSE::Init(a_skse);

	Init();

	return true;
}

EXTERN_C [[maybe_unused]] __declspec(dllexport) constinit auto SKSEPlugin_Version = []() noexcept {
	SKSE::PluginVersionData v;
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.HasNoStructUse();
	return v;
}();

EXTERN_C [[maybe_unused]] __declspec(dllexport) bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* pluginInfo)
{
	pluginInfo->name = SKSEPlugin_Version.pluginName;
	pluginInfo->infoVersion = SKSE::PluginInfo::kVersion;
	pluginInfo->version = SKSEPlugin_Version.pluginVersion;
	return true;
}
