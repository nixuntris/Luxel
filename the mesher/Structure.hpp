#pragma once
#include <cinttypes>
#include "Anubis/AnubisShaders.hpp"
#include "Anubis/Anubis.hpp"
#include "Misc.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <iostream>
#include <omp.h>

struct StructureDataHolder {
	uint16_t* values;
	uint8_t* positions;
	uint8_t width, height, depth;
	int voxelCount;
	Anubis::GPUONLYShortMap map;
	inline void Create(uint16_t* values, uint8_t* positions, int voxelCount, uint8_t width, uint8_t height, uint8_t depth) {
		this->values = values;
		this->positions = positions;
		this->voxelCount = voxelCount;
		this->width = width;
		this->height = height;
		this->depth = depth;
		
		map.Clear({ this->width,this->height,this->depth });
		map.Upload();
		map.GetBindlessHandle();

		for (int i = 0; i < voxelCount; i++) {
			
			
			uint16_t val = values[i];

			int x = (uint8_t)(positions[i * 3 + 0]);
			int y = (uint8_t)(positions[i * 3 + 1]);
			int z = (uint8_t)(positions[i * 3 + 2]);
			map.ChangePixel(x, y, z, val);
		}
	}
};

struct Structure {
	int variantCount;
	std::vector<StructureDataHolder*> variants;
	std::string name;
	
	void Create(std::string name) {
		this->name = name;
	}
	inline StructureDataHolder* GetRandomVariant() {
		int variant = Math::GetRandomValueStreamlinedFast(0, variantCount - 1);
		return this->variants[variant];
	}
	inline StructureDataHolder* GetSpecificVariant(int variant) {
		if (variant < 0 || variant >= variantCount) return nullptr;
		return this->variants[variant];
	}
	inline void AddVariant(uint16_t* values, uint8_t* positions, int voxelCount, uint8_t width, uint8_t height, uint8_t depth) {
		StructureDataHolder* variant = new StructureDataHolder;
		variant->Create(values, positions, voxelCount, width, height, depth);
		this->variants.push_back(variant);
		variantCount++;
	}
	~Structure() {
		for (auto t : variants) {
			delete[] t->values;
			delete[] t->positions;
			delete t;
		}
		variants.resize(0);
		variantCount = 0;
		name = "";
	}
};
auto addTreeVariants = [&](Structure* structure, int trunkHeight) {
	const int firstTierHeight = 6;
	const int tierSpacing = 4;

	const int straight[4][2] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
	const int diagonal[4][2] = { {1,1}, {-1,1}, {1,-1}, {-1,-1} };

	const int variantCount = 4;
	for (int v = 0; v < variantCount; v++) {
		std::vector<uint16_t> rawValues;
		std::vector<int> rawPositions;

		int minX = 0, minY = 0, minZ = 0;
		int maxX = 0, maxY = 0, maxZ = 0;

		auto addVoxel = [&](int x, int y, int z, uint16_t type) {
			rawValues.push_back(type);
			rawPositions.push_back(x);
			rawPositions.push_back(y);
			rawPositions.push_back(z);
			if (x < minX) minX = x;
			if (y < minY) minY = y;
			if (z < minZ) minZ = z;
			if (x > maxX) maxX = x;
			if (y > maxY) maxY = y;
			if (z > maxZ) maxZ = z;
			};

		float frameIndex = (float)v * 37.0f;
		float phase = (float)v * 2.1f;
		float sway =
			sin(float(v)*0.5 * 0.003f) * 0.65f
			+ sin(float(v) * 0.5 * 0.007f) * 0.20f
			+ cos(float(v) * 0.5 * 0.0045f) * 0.10f;
		int variation = v * 2;

		for (int y = 0; y < trunkHeight; y++)
			addVoxel(0, y, 0, WOOD);

		for (int tier = 0, h = firstTierHeight; h < trunkHeight - 1; h += tierSpacing, tier++) {
			const int (*dirs)[2] = (tier % 2 == 0) ? straight : diagonal;

			int length = 1 + (trunkHeight - h) * 6 / (trunkHeight - firstTierHeight) + (variation % 3) - 1;
			if (length < 1) length = 1;

			for (int i = 0; i < 4; i++) {
				int dirX = dirs[i][0], dirZ = dirs[i][1];
				int sideX = -dirZ, sideZ = dirX;

				for (int step = 1; step <= length; step++) {
					float stepSway = sway * sin(step * 0.9f + phase + tier * 0.5f);
					int swayX = (int)roundf(sideX * stepSway + dirX * stepSway * 0.4f);
					int swayZ = (int)roundf(sideZ * stepSway + dirZ * stepSway * 0.4f);
					int swayY = (int)roundf(stepSway * 0.5f);

					int x = dirX * step + swayX;
					int z = dirZ * step + swayZ;
					int y = h - step / 4 + swayY;

					if (step <= length - 3) {
						addVoxel(x, y, z, WOOD);
					}
					else {
						addVoxel(x, y, z, LEAVES);
						addVoxel(x + sideX, y, z + sideZ, LEAVES);
						addVoxel(x - sideX, y, z - sideZ, LEAVES);
						addVoxel(x, y + 1, z, LEAVES);
					}
				}

				float tipSway = sway * sin(length * 0.9f + phase + tier * 0.5f);
				addVoxel(dirX * length + (int)roundf(sideX * tipSway),
					h - length / 4 - 1 + (int)roundf(tipSway * 0.5f),
					dirZ * length + (int)roundf(sideZ * tipSway),
					LEAVES);
			}

			for (int i = 0; i < 4; i++) {
				float topSway = sway * cos(h * 0.5f + phase + i * 0.8f);
				addVoxel(straight[i][0] + (int)roundf(topSway),
					h + 1 + (int)roundf(topSway * 0.4f),
					straight[i][1] + (int)roundf(topSway * 0.6f),
					LEAVES);
			}
		}

		for (int i = 0; i < 4; i++) {
			float capSway = sway * sin((trunkHeight - 2) * 0.7f + phase + i * 0.9f);
			addVoxel(straight[i][0] + (int)roundf(capSway),
				trunkHeight - 2 + (int)roundf(capSway * 0.3f),
				straight[i][1] + (int)roundf(capSway * 0.5f), LEAVES);
			addVoxel(straight[i][0] + (int)roundf(capSway * 0.7f),
				trunkHeight - 1 + (int)roundf(capSway * 0.4f),
				straight[i][1] + (int)roundf(capSway * 0.4f), LEAVES);
		}

		float crownSway = sway * sin(trunkHeight * 0.6f + phase);
		addVoxel((int)roundf(crownSway * 0.3f),
			trunkHeight + (int)roundf(crownSway * 0.5f),
			(int)roundf(crownSway * 0.3f), LEAVES);
		addVoxel((int)roundf(crownSway * 0.5f),
			trunkHeight + 1 + (variation % 2) + (int)roundf(crownSway * 0.6f),
			(int)roundf(crownSway * 0.5f), LEAVES);

		int voxelCount = (int)rawValues.size();
		uint16_t* valArray = new uint16_t[voxelCount];
		uint8_t* posArray = new uint8_t[voxelCount * 3];

		for (int i = 0; i < voxelCount; i++) {
			valArray[i] = rawValues[i];
			posArray[i * 3 + 0] = (uint8_t)(rawPositions[i * 3 + 0] - minX);
			posArray[i * 3 + 1] = (uint8_t)(rawPositions[i * 3 + 1] - minY);
			posArray[i * 3 + 2] = (uint8_t)(rawPositions[i * 3 + 2] - minZ);
		}

		structure->AddVariant(valArray, posArray, voxelCount,
			(uint8_t)(maxX - minX + 1),
			(uint8_t)(maxY - minY + 1),
			(uint8_t)(maxZ - minZ + 1));
	}

	structure->variantCount = structure->variants.size();
};
auto addRockVariants = [&](Structure* structure, int minSize, int maxSize) {
	const int variantCount = 3;

	for (int v = 0; v < variantCount; v++) {
		std::vector<uint16_t> rawValues;
		std::vector<int> rawPositions;

		int minX = 0, minY = 0, minZ = 0;
		int maxX = 0, maxY = 0, maxZ = 0;

		auto addVoxel = [&](int x, int y, int z, uint16_t type) {
			rawValues.push_back(type);
			rawPositions.push_back(x);
			rawPositions.push_back(y);
			rawPositions.push_back(z);

			if (x < minX) minX = x;
			if (y < minY) minY = y;
			if (z < minZ) minZ = z;
			if (x > maxX) maxX = x;
			if (y > maxY) maxY = y;
			if (z > maxZ) maxZ = z;
		};

		int size = minSize + (maxSize - minSize) * v / (variantCount - 1);

		int radiusX = size;
		int radiusZ = size - (v == 1 ? 1 : 0);
		int height = size / 2 + 1 + v;
		for (int y = 0; y < height; y++) {
			float vertical = (float)(y + 1) / height;

			for (int x = -radiusX; x <= radiusX; x++) {
				for (int z = -radiusZ; z <= radiusZ; z++) {
					float nx = (float)x / radiusX;
					float nz = (float)z / radiusZ;

					float distance = nx * nx + nz * nz;
					float noise = ((x * 17 + z * 31 + y * 13 + v * 47) % 7) * 0.025f;

					float threshold = 1.0f - vertical * 0.25f + noise;

					if (distance > threshold) continue;
					if (y == height - 1 && distance > 0.35f + v * 0.1f) continue;

					addVoxel(x, y, z, STONE);
				}
			}
		}

		int voxelCount = (int)rawValues.size();

		uint16_t* valArray = new uint16_t[voxelCount];
		uint8_t* posArray = new uint8_t[voxelCount * 3];

		for (int i = 0; i < voxelCount; i++) {
			valArray[i] = rawValues[i];

			posArray[i * 3 + 0] = (uint8_t)(rawPositions[i * 3 + 0] - minX);
			posArray[i * 3 + 1] = (uint8_t)(rawPositions[i * 3 + 1] - minY);
			posArray[i * 3 + 2] = (uint8_t)(rawPositions[i * 3 + 2] - minZ);
		}

		structure->AddVariant(valArray,posArray,voxelCount,(uint8_t)(maxX - minX + 1),(uint8_t)(maxY - minY + 1),(uint8_t)(maxZ - minZ + 1));
	}

	structure->variantCount = structure->variants.size();
};
auto addSphereVariants = [&](Structure* structure, uint16_t type) {
	const int minRadius = 0;
	const int maxRadius = 16;

	const int variantCount = maxRadius - minRadius + 1;
	for (int v = 0; v < variantCount; v++) {
		std::vector<uint16_t> rawValues;
		std::vector<int> rawPositions;

		int minX = 0, minY = 0, minZ = 0;
		int maxX = 0, maxY = 0, maxZ = 0;

		auto addVoxel = [&](int x, int y, int z, uint16_t type) {
			rawValues.push_back(type);
			rawPositions.push_back(x);
			rawPositions.push_back(y);
			rawPositions.push_back(z);
			if (x < minX) minX = x;
			if (y < minY) minY = y;
			if (z < minZ) minZ = z;
			if (x > maxX) maxX = x;
			if (y > maxY) maxY = y;
			if (z > maxZ) maxZ = z;
			};

		int radius = minRadius + v;

		if (radius == 0) {
			addVoxel(0, 0, 0, type);
		}
		else {
			int r2 = radius * radius;
			for (int x = -radius; x <= radius; x++) {
				for (int y = -radius; y <= radius; y++) {
					for (int z = -radius; z <= radius; z++) {
						if (x * x + y * y + z * z <= r2) {
							bool isShell = (x * x + y * y + z * z) > (radius - 1) * (radius - 1);
							addVoxel(x, y, z, isShell ? 0 : type);
						}
					}
				}
			}
		}

		int voxelCount = (int)rawValues.size();
		uint16_t* valArray = new uint16_t[voxelCount];
		uint8_t* posArray = new uint8_t[voxelCount * 3];

		for (int i = 0; i < voxelCount; i++) {
			valArray[i] = rawValues[i];
			posArray[i * 3 + 0] = (uint8_t)(rawPositions[i * 3 + 0] - minX);
			posArray[i * 3 + 1] = (uint8_t)(rawPositions[i * 3 + 1] - minY);
			posArray[i * 3 + 2] = (uint8_t)(rawPositions[i * 3 + 2] - minZ);
		}

		structure->AddVariant(valArray, posArray, voxelCount,
			(uint8_t)(maxX - minX + 1),
			(uint8_t)(maxY - minY + 1),
			(uint8_t)(maxZ - minZ + 1));
	}

	structure->variantCount = structure->variants.size();
};


class StructureManager { //should store all the data but also handle the structure editor and viewer, which it currently doesn't 
public:
	int selectedStructure = 0;
	Structure structureList[maxAnimationObjects];
	int structureCount = 0;
	int choosenVariant = 0;
	int animationUpdateTicks[maxAnimationObjects];
	inline void Init() {
		for (int i = 0; i < maxAnimationObjects; i++) {
			animationUpdateTicks[i] = -1;
		}
		structureList[0].Create("tree");
		addTreeVariants(&structureList[0], 36);
		animationUpdateTicks[0] = 7;
		structureList[1].Create("rock");
		addRockVariants(&structureList[1], 3, 5);
		animationUpdateTicks[1] = 4;
		structureList[2].Create("Sphere");
		animationUpdateTicks[2] = 4;
		addSphereVariants(&structureList[2],20);
		ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(520, 480), ImGuiCond_FirstUseEver);
		structureCount = 2;
	}

	inline void EditorView() {

		ImGui::Begin("Structure Editor", nullptr, ImGuiWindowFlags_NoCollapse);

		ImGui::BeginChild("StructureList", ImVec2(200, 0), true);
		ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Structures");
		ImGui::Separator();
		for (int i = 0; i < structureCount; ++i) {
			ImGui::PushID(i);
			bool selected = (selectedStructure == i);
			if (selected) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.9f, 1.0f));
			}
			if (ImGui::Button(structureList[i].name.c_str(), ImVec2(-1, 0))) {
				if (i != selectedStructure) {
					choosenVariant = 0;
				}
				selectedStructure = i;
				
			}
			if (selected) ImGui::PopStyleColor();
			ImGui::PopID();
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("Preview", ImVec2(0, 0), true);
		ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "Preview");
		ImGui::Separator();

		ImVec2 p = ImGui::GetCursorScreenPos();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 rectMin = ImVec2(p.x, p.y);
		ImVec2 rectMax = ImVec2(p.x + avail.x, p.y + avail.y);

		ImDrawList* dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(rectMin, rectMax, IM_COL32(30, 30, 36, 255), 6.0f);
		dl->AddRect(rectMin, rectMax, IM_COL32(80, 80, 90, 255), 6.0f, 0, 2.0f);

		int gridW = structureList[selectedStructure].variants[0]->width;
		int gridH = structureList[selectedStructure].variants[0]->height;
		float cellW = avail.x / gridW;
		float cellH = avail.y / gridH;

		dl->PushClipRect(rectMin, rectMax, true);

		for (int gy = 0; gy < gridH; ++gy) {
			for (int gx = 0; gx < gridW; ++gx) {
				const bool even = ((gx + gy) & 1) == 0;
				ImU32 col = even ? IM_COL32(45, 45, 55, 255)
					: IM_COL32(60, 60, 72, 255);

				ImVec2 a(rectMin.x + gx * cellW, rectMin.y + gy * cellH);
				ImVec2 b(rectMin.x + (gx + 1) * cellW, rectMin.y + (gy + 1) * cellH);
				dl->AddRectFilled(a, b, col);
			}
		}

		dl->AddRectFilled({ 0,0 }, { 50,50 }, IM_COL32(255, 255, 255, 255), 0);
		dl->PopClipRect();
		ImGui::EndChild();

		ImGui::End();
	}
	
};

struct AnimatedObject {
	int currentAnimationTick;
	Math::Vector3 position;
	Math::Vector3 velocity = { 0,0,0 } ;
	int objectType;
	bool animationFrameUpdated;
	int oldAnimationTick;
	bool staticAnimation = false;
	bool hasGravity = false;
	AnimatedObject()
		: currentAnimationTick(0),
		objectType(0),
		animationFrameUpdated(false) {
	}
	AnimatedObject(Math::Vector3 position, int object) {
		this->position = position;
		this->currentAnimationTick = 2;
		this->oldAnimationTick = this->currentAnimationTick;
		this->objectType = object;

		animationFrameUpdated = true;
	}
	void Update(int frame, StructureManager *structureManager) {
		this->oldAnimationTick = this->currentAnimationTick;
		this->currentAnimationTick++;
		if (currentAnimationTick >= structureManager->structureList[objectType].variants.size()) currentAnimationTick = 0;

	}
};