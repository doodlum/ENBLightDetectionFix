#include "ENBLightAffectsStealth.h"

void ENBLightAffectsStealth::Reset()
{
	for (auto& particleLight : particleLights) {
		if (const auto particleSystem = netimmerse_cast<RE::NiParticleSystem*>(particleLight.first)) {
			if (auto particleData = particleSystem->GetParticleRuntimeData().particleData.get()) {
				particleData->DecRefCount();
			}
		}
		particleLight.first->DecRefCount();
	}
	particleLights.clear();
	std::swap(particleLights, queuedParticleLights);
}

float ENBLightAffectsStealth::CalculateLuminance(CachedParticleLight& light, RE::NiPoint3& point)
{
	// See BSLight::CalculateLuminance_14131D3D0
	// Performs lighting on the CPU which is identical to GPU code

	auto lightDirection = light.position - point;
	float lightDist = lightDirection.Length();
	float intensityFactor = std::clamp(lightDist / light.radius, 0.0f, 1.0f);
	float intensityMultiplier = 1 - intensityFactor * intensityFactor;

	return light.grey * intensityMultiplier;
}

void ENBLightAffectsStealth::AddParticleLightLuminance(RE::NiPoint3& targetPosition, int& numHits, float& lightLevel)
{
	std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
	particleLightsDetectionHits = 0;
	if (particleLightsEnabled) {
		for (auto& light : cachedParticleLights) {
			auto luminance = CalculateLuminance(light, targetPosition);
			lightLevel += luminance;
			if (luminance > 0.0)
				particleLightsDetectionHits++;
		}
	}
	numHits += particleLightsDetectionHits;
}

struct VertexColor
{
	std::uint8_t data[4];
};

void ENBLightAffectsStealth::CheckParticleLights(RE::BSRenderPass* a_pass, uint32_t a_technique)
{
	// See https://www.nexusmods.com/skyrimspecialedition/articles/1391
	if (a_technique == 0x4004146F || a_technique == 0x4004046F || a_technique == 0x4000046F) {
		if (auto shaderProperty = netimmerse_cast<RE::BSEffectShaderProperty*>(a_pass->shaderProperty)) {
			if (!shaderProperty->lightData) {
				if (auto material = shaderProperty->material) {
					if (!material->sourceTexturePath.empty()) {
						a_pass->geometry->IncRefCount();

						if (const auto particleSystem = netimmerse_cast<RE::NiParticleSystem*>(a_pass->geometry))
							if (auto particleData = particleSystem->GetParticleRuntimeData().particleData.get())
								particleData->IncRefCount();

						RE::NiColorA color;
						color.red = material->baseColor.red * material->baseColorScale;
						color.green = material->baseColor.green * material->baseColorScale;
						color.blue = material->baseColor.blue * material->baseColorScale;
						color.alpha = material->baseColor.alpha * shaderProperty->alpha;

						if (auto emittance = shaderProperty->unk88) {
							color.red *= emittance->red;
							color.green *= emittance->green;
							color.blue *= emittance->blue;
						}

						if (auto rendererData = a_pass->geometry->GetGeometryRuntimeData().rendererData) {
							if (rendererData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_COLORS)) {
								if (auto triShape = a_pass->geometry->AsTriShape()) {
									uint32_t vertexSize = rendererData->vertexDesc.GetSize();
									uint32_t offset = rendererData->vertexDesc.GetAttributeOffset(RE::BSGraphics::Vertex::Attribute::VA_COLOR);
									RE::NiColorA vertexColor{};
									for (int v = 0; v < triShape->GetTrishapeRuntimeData().vertexCount; v++) {
										if (VertexColor* vertex = reinterpret_cast<VertexColor*>(&rendererData->rawVertexData[vertexSize * v + offset])) {
											RE::NiColorA niColor{ (float)vertex->data[0] / 255.0f, (float)vertex->data[1] / 255.0f, (float)vertex->data[2] / 255.0f, (float)vertex->data[3] / 255.0f };
											if (niColor.alpha > vertexColor.alpha)
												vertexColor = niColor;
										}
									}
									color.red *= vertexColor.red;
									color.green *= vertexColor.green;
									color.blue *= vertexColor.blue;
									if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexAlpha))
										color.alpha *= vertexColor.alpha;
								}
							}
						}
						queuedParticleLights.insert({ a_pass->geometry, { color } });
					}
				}
			}
		}
	}
}

float ENBLightAffectsStealth::CalculateLightDistance(float3 a_lightPosition, float a_radius)
{
	return (a_lightPosition.x * a_lightPosition.x) + (a_lightPosition.y * a_lightPosition.y) + (a_lightPosition.z * a_lightPosition.z) - (a_radius * a_radius);
}

bool ENBLightAffectsStealth::AddCachedParticleLights(ENBLightAffectsStealth::LightData& light, RE::BSGeometry* a_geometry, double a_timer)
{
	static float& lightFadeStart = (*(float*)REL::RelocationID(527668, 414582).address());
	static float& lightFadeEnd = (*(float*)REL::RelocationID(527669, 414583).address());

	if ((light.color.x + light.color.y + light.color.z) > 1e-4 && light.radius > 1e-4) {
		CachedParticleLight cachedParticleLight{};
		cachedParticleLight.grey = float3(light.color.x, light.color.y, light.color.z).Dot(float3(0.3f, 0.59f, 0.11f));
		cachedParticleLight.radius = light.radius;
		cachedParticleLight.position = { light.positionWS.x, light.positionWS.y, light.positionWS.z };

		cachedParticleLights.push_back(cachedParticleLight);

		return true;
	}

	return false;
}

void ENBLightAffectsStealth::UpdateLights()
{
	std::lock_guard<std::shared_mutex> lk{ cachedParticleLightsMutex };
	cachedParticleLights.clear();

	LightData clusteredLight{};
	uint32_t clusteredLights = 0;

	for (const auto& particleLight : particleLights) {
		if (const auto particleSystem = netimmerse_cast<RE::NiParticleSystem*>(particleLight.first);
			particleSystem && particleSystem->GetParticleRuntimeData().particleData.get()) {
			// process BSGeometry
			auto particleData = particleSystem->GetParticleRuntimeData().particleData.get();

			auto numVertices = particleData->GetActiveVertexCount();
			for (uint32_t p = 0; p < numVertices; p++) {
				float radius = particleData->GetParticlesRuntimeData().sizes[p] * 64;

				auto initialPosition = particleData->GetParticlesRuntimeData().positions[p];
				if (!particleSystem->GetParticleSystemRuntimeData().isWorldspace) {
					// detect first-person meshes
					if ((particleLight.first->GetModelData().modelBound.radius * particleLight.first->world.scale) != particleLight.first->worldBound.radius)
						initialPosition += particleLight.first->worldBound.center;
					else
						initialPosition += particleLight.first->world.translate;
				}

				RE::NiPoint3 positionWS = initialPosition;

				if (clusteredLights) {
					auto averageRadius = clusteredLight.radius / (float)clusteredLights;
					float radiusDiff = abs(averageRadius - radius);

					auto averagePosition = clusteredLight.positionWS / (float)clusteredLights;
					float positionDiff = positionWS.GetDistance({ averagePosition.x, averagePosition.y, averagePosition.z });

					if ((radiusDiff + positionDiff) > 32.0f) {
						clusteredLight.radius /= (float)clusteredLights;
						clusteredLight.positionWS /= (float)clusteredLights;
						clusteredLight.positionWS = clusteredLight.positionWS;

						AddCachedParticleLights(clusteredLight);

						clusteredLights = 0;
						clusteredLight.color = { 0, 0, 0 };
						clusteredLight.radius = 0;
						clusteredLight.positionWS = { 0, 0, 0 };
					}
				}

				float alpha = particleLight.second.color.alpha * particleData->GetParticlesRuntimeData().color[p].alpha;
				float3 color;
				color.x = particleLight.second.color.red * particleData->GetParticlesRuntimeData().color[p].red;
				color.y = particleLight.second.color.green * particleData->GetParticlesRuntimeData().color[p].green;
				color.z = particleLight.second.color.blue * particleData->GetParticlesRuntimeData().color[p].blue;
				clusteredLight.color += color * alpha * 3.0;

				clusteredLight.radius += radius;
				clusteredLight.positionWS.x += positionWS.x;
				clusteredLight.positionWS.y += positionWS.y;
				clusteredLight.positionWS.z += positionWS.z;

				clusteredLights++;
			}
		} else {
			// process billboard
			LightData light{};

			light.color.x = particleLight.second.color.red;
			light.color.y = particleLight.second.color.green;
			light.color.z = particleLight.second.color.blue;
			light.color *= particleLight.second.color.alpha * 0.5;

			float radius = particleLight.first->GetModelData().modelBound.radius * particleLight.first->world.scale;
			light.radius = radius;
			light.positionWS = { particleLight.first->worldBound.center.x, particleLight.first->worldBound.center.y, particleLight.first->worldBound.center.z };

			AddCachedParticleLights(light, particleLight.first);
		}
	}

	if (clusteredLights) {
		clusteredLight.radius /= (float)clusteredLights;
		clusteredLight.positionWS /= (float)clusteredLights;
		AddCachedParticleLights(clusteredLight);
	}
}

void ENBLightAffectsStealth::UpdateSettings(ENB_API::ENBSDKALT1002* a_enb)
{
	ENBParameter param;
	param.Type = ENB_SDK::ENBParameterType::ENBParam_BOOL;
	param.Size = ENB_SDK::ENBParameterTypeToSize(param.Type);

	if (a_enb->GetParameter("enbseries.ini", "EFFECT", "EnableComplexParticleLights", &param))
		memcpy(&particleLightsEnabled, param.Data, param.Size);
}
