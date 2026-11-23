#pragma once

#include "Anubis/Anubis.hpp"
#include "Anubis/AnubisShaders.hpp"
#include "Anubis/AnubisMath.hpp"
#include <sys/stat.h> 
const int maxAnimationBatch = 128;
const int mwx = 300;
const int mwy = 32;
const int mwz = 300;
const int maxAnimationObjects = 1024;
const int maxGPUBatch = 1024 * 1024;
const int maxGrassCount = 1449303 * 2;
inline uint32_t packRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return (uint32_t(r)) |
		(uint32_t(g) << 8) |
		(uint32_t(b) << 16) |
		(uint32_t(a) << 24);
}
inline bool CustomToggle(const char* label, bool* value) {
	ImVec2 pos = ImGui::GetCursorScreenPos();
	ImVec2 size = ImVec2(32, 32);
	bool clicked = ImGui::InvisibleButton(label, size);
	if (clicked) *value = !*value;

	ImU32 color = *value ? IM_COL32(255, 255, 255, 255) : IM_COL32(50, 50, 50, 255);
	ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), color, 4.0f);

	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::Text("%s", label);
	return clicked;
}
const std::vector<Color> palette = {
	{0, 255, 0, 255},       // 0: GREEN GRASS
	{128, 128, 128, 255},   // 1: GRAY
	{153, 76, 0, 255},      // 2: BROWN
	{92, 64, 51, 255},      // 3: DARKBROWN
	{0, 117, 44, 255},      // 4: DARKGREEN
	{255, 255, 255, 255},   // 5: WHITE
	{0, 0, 128, 255},       // 6: DARKBLUE
	{85, 158, 47, 255},     // 7: CUSTOM GREEN
	{238, 214, 175, 255},   // 8: DESERT SAND
	{ 28,163,236,255 },   // 9: BLUE
	{0, 121, 36, 255},      // 10: Darker Grass Shade
	{0, 128, 49, 255},      // 11: Mid-Light Grass Variant
	{2, 137, 53, 255},      // 12: Brightest Grass Highlight
	{0, 110, 41, 255},      // 13: Medium Grass Blend
	{0, 106, 39, 255},       // 14: Deepest Grass Shadow
	{54, 34, 15, 255},      // 15
	{92, 64, 51, 255},      // 16
	{153, 77, 0, 255},      // 17
	{238, 214, 175, 255},    // 18
	{ 59,  87,  44, 255 },  // 10: Leaf Highlight (Replaced the neon yellow with a bright, natural leaf green)
	{ 48,  71,  36, 255 },  // 11: Main Canopy Leaf Green (Replaced Warm Sand)
	{ 40,  61,  30, 255 },  // 12: Medium Canopy Leaf Green (Replaced Light Camel)
	{ 33,  51,  25, 255 },  // 13: Dark/Shadow Leaf Green (Replaced Peach/Tan)
	{ 83,  62,  46, 255 },  // 14: Outer Branch Brown (Replaced Bright Olive)
	{ 69,  50,  36, 255 },  // 15: Inner Heavy Branch Brown (Replaced Intense Lemon)
	{ 56,  40,  29, 255 },  // 16: Deep Core Wood/Shadow Brown (Replaced Pale Yellow)
	{ 53,  76,  40, 255 },  // 17: Alternative Leaf Green (Replaced Pear Green)
	{ 45,  66,  33, 255 },   // 18: Alternative Leaf Green (Replaced Apple Green)
	{255,255,255,255},
	{0,0,0,255}
};
enum VOXEL_ENUM_NAMES {
	GRASS = 1,   // palette[0]
	STONE = 2,   // palette[1]
	DIRT = 3,   // palette[2]
	WOOD = 4,   // palette[3]
	LEAVES = 5,   // palette[4]
	SNOW = 6,   // palette[5]
	WATER = 7,   // palette[6]
	SAND = 9,   // palette[8]
	OLIVE = 10,   // palette[9]
	LIGHT_BARRIER = 255
};
struct ShaderManager {
	Anubis::DisplayShader display;
	Anubis::DisplayShader hwRender;
	Anubis::DisplayShader hwRenderForObjects;
	Anubis::ComputeShader rasterizer;
	Anubis::ComputeShader frustumCull;
	Anubis::ComputeShader hizBuilder;
	Anubis::ComputeShader meshletRenderer;
	Anubis::ComputeShader grassRenderer;
	Anubis::ComputeShader secondaryRaysRender;
	Anubis::ComputeShader generateMesh;
	Anubis::ComputeShader clouds;
	Anubis::ComputeShader bilboards;
	Anubis::ComputeShader primaryRays;
	Anubis::ComputeShader prepassPrimaryRays;
	Anubis::ComputeShader rasterizeAnimatedObjects;
	Anubis::ComputeShader setAnimatedObjects;
	Anubis::ComputeShader clearAnimatedObjects;
	Anubis::ComputeShader simulateWater;
	Anubis::ComputeShader simulateWaterBatched;
	Anubis::ComputeShader renderWater;
	void Load() {

		std::string shaderDir = "";
		struct stat info;
		if (stat("shaders", &info) == 0 && (info.st_mode & S_IFDIR)) {
			shaderDir = "shaders/";
			std::cout << "Found shader dir\n";
		}

		auto path = [&](const std::string& name) -> std::string {
			return shaderDir + name;
			};

		display.LoadFromFile(
			path("quad_display_shader.fs").c_str(),
			"Anubis/BaseShaders/quad_display_shader.vs"
		);
		primaryRays.Setup(path("PrimaryRays.glsl").c_str());
		rasterizer.Setup(path("Rasterize.glsl").c_str());
		frustumCull.Setup(path("frustumCull.glsl").c_str());
		hizBuilder.Setup(path("HizBuilder.glsl").c_str());
		meshletRenderer.Setup(path("renderAABB.glsl").c_str());
		grassRenderer.Setup(path("grassParticle.glsl").c_str());
		hwRender.LoadFromFile(path("fes.fs").c_str(), path("ver.vs").c_str());
		hwRenderForObjects.LoadFromFile(path("hwMesh.fs").c_str(), path("hwMesh.vs").c_str());
		secondaryRaysRender.Setup(path("SecondaryRays.glsl").c_str());
		generateMesh.Setup(path("GenerateMesh.glsl").c_str());
		clouds.Setup(path("Clouds.glsl").c_str());
		bilboards.Setup(path("bilboard.glsl").c_str());
		prepassPrimaryRays.Setup(path("PrePassPrimary.glsl").c_str());
		rasterizeAnimatedObjects.Setup(path("RasterizeAnimatedObjects.glsl"));
		setAnimatedObjects.Setup(path("SetAnimatedData.glsl"));
		clearAnimatedObjects.Setup(path("ClearAnimatedData.glsl"));
		simulateWater.Setup(path("SimulateObjects.glsl"));
		simulateWaterBatched.Setup(path("SimulateObjectsBatched.glsl"));
		renderWater.Setup(path("WaterTrace.glsl"));
	}

};
const inline Matrix GetStaticMVPMatrix(Vector3 position, Matrix matView) {
	const float aspect = (float)3840 / (float)2160;
	Matrix matModel = MatrixTranslate(position.x, position.y, position.z);
	Matrix matViewProj = MatrixMultiply(matView, matProj);
	return MatrixMultiply(matModel, matViewProj);
}
void Render(ShaderManager* shaders, Anubis::Camera camera, Matrix matView, Anubis::HWVIrGeomMesh& renderSphere, Vector3 vv, int frame, int voxelType, int size, uint32_t* pack, int packCount, int vHeight) {

	Anubis::SwitchShaderStage(shaders->hwRender.shaderId);

	Matrix m = GetStaticMVPMatrix(vv, matView);
	shaders->hwRender.SetVector3(camera.position, "camPos");
	Math::Vector3 camFwd = Math::NormalizeVector3(
		Math::SubtractVector3(camera.target, camera.position)
	);
	shaders->hwRender.SetVector3(camFwd, "camForward");
	shaders->hwRender.SetUIntArray(pack, packCount, "packedColors");
	shaders->hwRender.SetFloat(size, "size");
	shaders->hwRender.SetInt(frame, "frame");
	shaders->hwRender.SetInt(voxelType, "voxelType");
	shaders->hwRender.SetInt(vHeight, "vHeight");
	shaders->hwRender.SetVector3({ 1,0,1 }, "colorr");
	shaders->hwRender.SetMatrix({ m.m0 ,m.m1,m.m2,m.m3,m.m4,m.m5,m.m6,m.m7,m.m8,m.m9,m.m10,m.m11,m.m12,m.m13,m.m14,m.m15 }, "MVP");
	shaders->hwRender.Use();

	renderSphere.Draw();
}
struct Settings {
	struct Resolution {
		int width;
		int height;
		std::string name;
	};
	bool renderClouds = true;
	bool grassRender = true;
	bool secondaryRays = true;
	bool simulatePhysics = true;
	bool renderFluids = true;
	bool globalIllumination = false;
	bool godMode = false;
	bool renderResolution = false;
	bool hardwareSoftwareRendering = true;
	bool FXAA = false;
	Resolution resolutions[10] = {
		{640,480,"SD.480p"},
		{1280,720,"HD/720p"},
		{1920,1080,"FHD/1080p"},
		{2560,1440, "QHD/1440P"},
		{3840,2160, "UHD/4k"}
	};
};

