#pragma once
#include <ENB/ENBSeriesAPI.h>
#include <shared_mutex>

struct ENBLightAffectsStealth
{
public:
	static ENBLightAffectsStealth* GetSingleton()
	{
		static ENBLightAffectsStealth render;
		return &render;
	}

	static void InstallHooks()
	{
		Hooks::Install();
	}

	struct LightData
	{
		float3 color;
		float radius;
		float3 positionWS;
	};

	struct CachedParticleLight
	{
		float grey;
		RE::NiPoint3 position;
		float radius;
	};

	struct ParticleLightInfo
	{
		RE::NiColorA color;
	};

	BOOL particleLightsEnabled = false;

	eastl::hash_map<RE::BSGeometry*, ParticleLightInfo> queuedParticleLights;
	eastl::hash_map<RE::BSGeometry*, ParticleLightInfo> particleLights;

	float CalculateLightDistance(float3 a_lightPosition, float a_radius);
	bool AddCachedParticleLights(ENBLightAffectsStealth::LightData& light, RE::BSGeometry* a_geometry = nullptr, double timer = 0.0f);
	void UpdateLights();
	void UpdateSettings(ENB_API::ENBSDKALT1002* a_enb);

	void CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique);

	std::shared_mutex cachedParticleLightsMutex;
	eastl::vector<CachedParticleLight> cachedParticleLights;
	uint32_t particleLightsDetectionHits = 0;

	void Reset();

	float CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point);
	void AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel);

	struct Hooks
	{
		struct BSBatchRenderer__RenderPassImmediately1
		{
			static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
			{
				GetSingleton()->CheckParticleLights(Pass, Technique);
				func(Pass, Technique, AlphaTest, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately2
		{
			static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
			{
				GetSingleton()->CheckParticleLights(Pass, Technique);
				func(Pass, Technique, AlphaTest, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct BSBatchRenderer__RenderPassImmediately3
		{
			static void thunk(RE::BSRenderPass* Pass, uint32_t Technique, bool AlphaTest, uint32_t RenderFlags)
			{
				GetSingleton()->CheckParticleLights(Pass, Technique);
				func(Pass, Technique, AlphaTest, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct AIProcess_CalculateLightValue_GetLuminance
		{
			static float thunk(RE::ShadowSceneNode* shadowSceneNode, RE::NiPoint3& targetPosition, int& numHits, float& sunLightLevel, float& lightLevel, RE::NiLight& refLight, int32_t shadowBitMask)
			{
				auto ret = func(shadowSceneNode, targetPosition, numHits, sunLightLevel, lightLevel, refLight, shadowBitMask);
				GetSingleton()->AddParticleLightLuminance(targetPosition, numHits, ret);
				return ret;
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately1>(REL::RelocationID(100877, 107673).address() + REL::Relocate(0x1E5, 0x1EE));
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately2>(REL::RelocationID(100852, 107642).address() + REL::Relocate(0x29E, 0x28F));
			stl::write_thunk_call<BSBatchRenderer__RenderPassImmediately3>(REL::RelocationID(100871, 107667).address() + REL::Relocate(0xEE, 0xED));

			stl::write_thunk_call<AIProcess_CalculateLightValue_GetLuminance>(REL::RelocationID(38900, 39946).address() + REL::Relocate(0x1C9, 0x1D3));

			logger::info("Installed hooks");
		}
	};
};
