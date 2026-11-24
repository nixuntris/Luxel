#pragma once
#include <cinttypes>
#include "Anubis/AnubisShaders.hpp"
#include "Anubis/Anubis.hpp"
#include "NoiseGen.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <iostream>
#include <omp.h>
#include "Structure.hpp"

#include "Misc.hpp"
#define IDX(x,y,z,size) ((x) + (y) * size + (z) * size * size)
#define INDEX(x, y, z, width, height) ((x) + (y) * (width) + (z) * (width) * (height))
#define FACE_FRONT  (1 << 0)
#define FACE_BACK   (1 << 1)
#define FACE_LEFT   (1 << 2)
#define FACE_RIGHT  (1 << 3)
#define FACE_TOP    (1 << 4)
#define FACE_BOTTOM (1 << 5)
const int FACES[6] = { FACE_FRONT,FACE_BACK,FACE_LEFT,FACE_RIGHT,FACE_TOP,FACE_BOTTOM };

enum CHUNKTYPE {
	DESERT = 0,
	SAVANNA = 1,
	BEACH = 2,
	MOUNTAIN = 3,
	CAVES = 4,
	OCEAN
};
float caveScale = 0.025f;
float detailRatio = 2.1428f;

const float caveDetailScale = caveScale * detailRatio;

struct ChunkData {
	bool shortenedData = false;
	bool skipped = false;
	int lod;
	Anubis::GPUONLYShortMap fluidShortMap;
	Anubis::GPUONLYByteMap fluidByteMap;
	Anubis::GPUONLYByteMap probeMap;
	Anubis::GPUONLYByteMap waterMap;
	uint16_t* terrain;
	uint16_t* palette;
	int paletteSize;
	uint8_t* compressedTerrain;
	bool compressed;
	int voxelCount = 0;
	int lastChunkWaterActivity = 0;
	bool updated = false;
};

class Chunk {
	int tallestY = 0;
public:
	int gx;
	int gy;
	int gz;
	int oldTri;
	bool updated = false;
	int allocatedTriSize = 0;
	bool hardwareRasterize = false;
	int size;
	int lod;
	bool generatedAtLod = false;
	int voxelCount = 0;
	int endTri;
	int beginningTri;
	std::vector<Vector3> grassList;
	inline void GeneratePerType(CHUNKTYPE type, ChunkData* chunkData, GenerationData generationData, int gx, int gy, int gz, int lod = 1, bool grass = false) {

		switch (type) {
		case DESERT:
			GenerateDesert(chunkData, generationData, gx, gy, gz, lod, grass);
			break;
		case SAVANNA:
			GenerateSavanna(chunkData, generationData, gx, gy, gz, lod, grass);
			break;
		case MOUNTAIN:
			GenerateMountains(chunkData, generationData, gx, gy, gz, lod, grass); //the problem is grass, specifically if it's set to true, it makes the program randomly hang after generation
			break;
		case OCEAN:
			GenerateDesert(chunkData, generationData, gx, gy, gz, lod, grass);
			break;
		}
	}
	inline void GenerateSavanna(ChunkData* chunkData, GenerationData generationData, int gx, int gy, int gz, int lod = 1, bool grass = false) {
		this->gx = gx;
		this->gy = gy;
		this->gz = gz;
		this->size = chunkSize / lod;
		this->lod = lod;
		chunkData->terrain = (uint16_t*)MemAlloc(chunkSize * chunkSize * chunkSize * sizeof(uint16_t));

		const int maxSavannaY = 250;
		if (gy * chunkSize > maxSavannaY) {
			chunkData->skipped = true;
			return;
		}

		const int baseWX = gx * chunkSize;
		const int baseWY = gy * chunkSize;
		const int baseWZ = gz * chunkSize;

		for (int x = 0; x < chunkSize; x++) {
			for (int z = 0; z < chunkSize; z++) {
				float wxf = (float)(baseWX + x);
				float wzf = (float)(baseWZ + z);
				float broad = perlin_noise_2d(wxf * 0.004f, wzf * 0.004f) * 10.0f;
				float small = perlin_noise_2d(wxf * 0.020f, wzf * 0.020f) * 2.5f;
				int terrainHeight = 135 + (int)(broad + small) + 17;

				float dryness = perlin_noise_2d(wxf * 0.03f, wzf * 0.03f);
				float redPatch = perlin_noise_2d(wxf * 0.012f + 50.0f, wzf * 0.012f + 50.0f);
				float fine = SavRand01(baseWX + x, baseWZ + z, 7);

				uint16_t topBlock;
				if (redPatch > 0.45f) topBlock = 18;
				else if (dryness > 0.15f) topBlock = (fine > 0.8f ? 10 : 9);
				else topBlock = (fine > 0.7f ? 9 : 10);

				for (int y = chunkSize - 1; y >= 0; y--) {
					int worldY = baseWY + y;
					if (worldY >= terrainHeight) continue;

					uint16_t v;
					if (worldY == terrainHeight - 1) v = topBlock;
					else if (worldY >= terrainHeight - 4) v = 18;
					else if (worldY >= terrainHeight - 7) v = 4;
					else v = 2;

					chunkData->terrain[IDX(x, y, z, chunkSize)] = v;
				}

				int tuftY = terrainHeight - baseWY;
				if (tuftY >= 0 && tuftY < chunkSize - 2 && topBlock != 18) {
					float r = SavRand01(baseWX + x, baseWZ + z, 31);
					if (r < 0.14f) {
						int h = 1 + (int)(SavRand01(baseWX + x, baseWZ + z, 32) * 2.0f);
						for (int t = 0; t < h; t++)
							chunkData->terrain[IDX(x, tuftY + t, z, chunkSize)] = (r < 0.05f ? 9 : 8);
					}
					else if (r > 0.995f) {
						chunkData->terrain[IDX(x, tuftY, z, chunkSize)] = 21;
						if (x + 1 < chunkSize) chunkData->terrain[IDX(x + 1, tuftY, z, chunkSize)] = 27;
						chunkData->terrain[IDX(x, tuftY + 1, z, chunkSize)] = 20;
					}
				}
			}
		}



	}
	inline void GenerateCaves(ChunkData* chunkData, int gx, int gy, int gz, int lod = 1) {
		this->gx = gx;
		this->gy = gy;
		this->gz = gz;
		this->size = chunkSize / lod;
		this->lod = lod;
		chunkData->terrain = (uint16_t*)MemAlloc(chunkSize * chunkSize * chunkSize * sizeof(uint16_t));

		const int baseWX = gx * chunkSize;
		const int baseWY = gy * chunkSize;
		const int baseWZ = gz * chunkSize;
		float noiseValues[chunkSize / 4][chunkSize / 4][chunkSize / 4];
		for (int x = 0; x < chunkSize / 4; x++) {

			int worldX = baseWX + x * 4;
			for (int y = 0; y < chunkSize / 4; y++) {
				int worldY = baseWY + y * 4;
				for (int z = 0; z < chunkSize / 4; z++) {

					int worldZ = baseWZ + z * 4;

					float broad = perlin_noise_3d(
						worldX * caveScale,
						worldY * caveScale,
						worldZ * caveScale
					);
					float detail = perlin_noise_3d(
						worldX * caveDetailScale,
						worldY * caveDetailScale,
						worldZ * caveDetailScale
					);
					noiseValues[x][y][z] = broad * 0.75f + detail * 0.25f;
				}
			}
		}
		for (int x = 0; x < chunkSize; x++) {
			for (int y = 0; y < chunkSize; y++) {
				int worldY = baseWY + y;
				for (int z = 0; z < chunkSize; z++) {
					int worldX = baseWX + x;
					int worldZ = baseWZ + z;

					float cave = noiseValues[x / 4][y / 4][z / 4];

					chunkData->terrain[IDX(x, y, z, chunkSize)] = (cave > 0.32f) * STONE;
				}
			}
		}
	}
	inline void GenerateDesert(ChunkData* chunkData, GenerationData generationData, int gx, int gy, int gz, int lod = 1, bool grass = false) {
		this->gx = gx;
		this->gy = gy;
		this->gz = gz;
		START_TIMER(total)
			this->size = chunkSize / lod;
		chunkData->terrain = (uint16_t*)MemAlloc(chunkSize * chunkSize * chunkSize * sizeof(uint16_t));
		this->lod = lod;
		START_TIMER(voxel_fill)
			if ((gy + 1 < generationData.lowestChunkY) / 5 || (gy > generationData.highestChunkY) / 5) {
				chunkData->skipped = true;
				return;
			}
		for (uint8_t x = 0; x < chunkSize; x += 1) {
			for (uint8_t z = 0; z < chunkSize; z += 1) {
				int terrainHeight = generationData.heightMap[x + z * chunkSize] / 5 + 125;
				int grassLineOffset = Math::GetRandomValueStreamlinedFast(-50, 50);
				for (int y = chunkSize - 1; y >= 0; y -= 1) {
					int worldY = gy * chunkSize + y;
					if (worldY < terrainHeight - 6) {
						chunkData->terrain[
							IDX(x, y, z, chunkSize)
						] = 9; // sand
						if (chunkData->terrain[IDX(x, y + 1, z, chunkSize)] == 0) {
							if ((x + y + Math::GetRandomValueStreamlinedFast(-1, 1)) % 2 == 0 && grass && Math::GetRandomValueStreamlinedFast(0, 1000) == 150) grassList.push_back({ (float)x + gx * chunkSize,(float)y + gy * chunkSize,(float)z + gz * chunkSize });
						}
					}
				}
			}
		}
	}
	inline void GenerateMountains(ChunkData* chunkData, GenerationData generationData, int gx, int gy, int gz, int lod = 1, bool grass = false) {
		this->gx = gx;
		this->gy = gy;
		this->gz = gz;
		START_TIMER(total)
			if (lod == 0) {
				chunkData->skipped = true;
				return;
			}
		this->size = chunkSize / lod;
		this->lod = lod;
		START_TIMER(voxel_fill)
			if (gy * chunkSize + chunkSize< generationData.lowestChunkY * chunkSize || gy * chunkSize> generationData.highestChunkY * chunkSize) {
				chunkData->skipped = true;
				return;
			}

		chunkData->compressedTerrain = (uint8_t*)MemAlloc(chunkSize * chunkSize * chunkSize * sizeof(uint8_t));
		chunkData->shortenedData = true;
		if (grass) grassList.resize(32 * 32);
		for (uint8_t x = 0; x < chunkSize; x += 1) {
			for (uint8_t z = 0; z < chunkSize; z += 1) {
				int terrainHeight = generationData.heightMap[x + z * chunkSize];
				int grassLineOffset = Math::GetRandomValueStreamlinedFast(-50, 50);
				for (int y = chunkSize - 1; y >= 0; y -= 1) {
					int worldY = gy * chunkSize + y;
					if (worldY < terrainHeight - 6) {
						chunkData->compressedTerrain[
							IDX(x, y, z, chunkSize)
						] = 2; // stone

					}
					else if (worldY < terrainHeight) {
						if (worldY < 200 + grassLineOffset) {
							chunkData->compressedTerrain[
								IDX(x, y, z, chunkSize)
							] = 5; //dirt
							if (chunkData->compressedTerrain[
								IDX(x, y + 1, z, chunkSize)
							] == 0) {
								if ((x + y + Math::GetRandomValueStreamlinedFast(-1, 1)) % 2 == 0 && grass) grassList.push_back({ (float)x + gx * chunkSize,(float)y + gy * chunkSize,(float)z + gz * chunkSize });
							}
						}
						else if (worldY < 400 + grassLineOffset) {

							chunkData->compressedTerrain[
								IDX(x, y, z, chunkSize)
							] = 2; //stone

						}
						else {
							chunkData->compressedTerrain[
								IDX(x, y, z, chunkSize)
							] = 6; //snow


						}
					}
				}
			}
		}
	}
	inline void CarveCaves(ChunkData* chunkData, int gx, int gy, int gz, int lod = 1) {
		const int baseWX = gx * chunkSize;
		const int baseWY = gy * chunkSize;
		const int baseWZ = gz * chunkSize;

		const float surfaceThreshold = 0.45f;
		const float deepThreshold = 0.28f;
		const int   surfaceWorldY = 220;
		const int   deepWorldY = 40;

		float noiseValues[chunkSize / 4][chunkSize / 4][chunkSize / 4];
		for (int x = 0; x < chunkSize / 4; x++) {

			int worldX = baseWX + x * 4;
			for (int y = 0; y < chunkSize / 4; y++) {
				int worldY = baseWY + y * 4;
				for (int z = 0; z < chunkSize / 4; z++) {

					int worldZ = baseWZ + z * 4;

					float broad = perlin_noise_3d(
						worldX * caveScale,
						worldY * caveScale,
						worldZ * caveScale
					);
					float detail = perlin_noise_3d(
						worldX * caveDetailScale,
						worldY * caveDetailScale,
						worldZ * caveDetailScale
					);
					noiseValues[x][y][z] = broad * 0.75f + detail * 0.25f;
				}
			}
		}
		for (int x = 0; x < chunkSize; x += 1) {
			for (int y = 0; y < chunkSize; y += 1) {
				int worldY = baseWY + y;
				for (int z = 0; z < chunkSize; z += 1) {
					uint16_t& cell = chunkData->terrain[IDX(x, y, z, chunkSize)];

					if (cell != 2 && cell != 5) continue;

					int worldX = baseWX + x;
					int worldZ = baseWZ + z;

					float wx = (float)worldX;
					float wy = (float)worldY;
					float wz = (float)worldZ;

					float cave = noiseValues[x / 4][y / 4][z / 4];
					float t = Math::Clamp((float)(surfaceWorldY - worldY) /
						(float)(surfaceWorldY - deepWorldY), 0.f, 1.f);
					float threshold = Lerp(surfaceThreshold, deepThreshold, t);

					if (cave > threshold) {
						cell = 0;
					}
				}
			}
		}
	}
	inline void GenerateLod(ChunkData* chunkData, int gx, int gy, int gz) {

		if (generatedAtLod) return;
		voxelCount = 0;
		if (chunkData->shortenedData || chunkData->compressed) {
			for (int i = 0; i < chunkSize * chunkSize * chunkSize; i++) {
				voxelCount += chunkData->compressedTerrain[i] != 0;
			}
			chunkData->voxelCount = voxelCount;
			if (voxelCount == 0) {
				free(chunkData->compressedTerrain);
				voxelCount = 0;
				return;
			}
			if (lod != 1) {
				START_TIMER(lod_generation)
					uint8_t* lodVals = (uint8_t*)MemAlloc(this->size * this->size * this->size * sizeof(uint8_t));

				for (int x = 0; x < this->size; x++) {
					for (int z = 0; z < this->size; z++) {
						for (int y = 0; y < this->size; y++) {
							for (int cx = 0; cx < this->lod; cx++) {
								for (int cy = 0; cy < this->lod; cy++) {
									for (int cz = 0; cz < this->lod; cz++) {
										if (chunkData->compressedTerrain[IDX(x * this->lod + cx, y * this->lod + cy, z * this->lod + cz, chunkSize)] != 0) {
											lodVals[IDX(x, y, z, this->size)] = chunkData->compressedTerrain[IDX(x * this->lod + cx, y * this->lod + cy, z * this->lod + cz, chunkSize)];
											break;
											break;
											break;
										}
									}
								}
							}

						}
					}
				}
				MemFree(chunkData->compressedTerrain);

				chunkData->compressedTerrain = lodVals;
			}
			chunkData->lod = lod;
			return;
		}
		else {
			for (int i = 0; i < chunkSize * chunkSize * chunkSize; i++) {
				voxelCount += chunkData->terrain[i] != 0;
			}
			chunkData->voxelCount = voxelCount;
			if (voxelCount == 0) {
				free(chunkData->terrain);
				voxelCount = 0;
				return;
			}
			if (lod != 1) {
				START_TIMER(lod_generation)
					uint16_t* lodVals = (uint16_t*)MemAlloc(this->size * this->size * this->size * sizeof(uint16_t));

				for (int x = 0; x < this->size; x++) {
					for (int z = 0; z < this->size; z++) {
						for (int y = 0; y < this->size; y++) {
							for (int cx = 0; cx < this->lod; cx++) {
								for (int cy = 0; cy < this->lod; cy++) {
									for (int cz = 0; cz < this->lod; cz++) {
										if (chunkData->terrain[IDX(x * this->lod + cx, y * this->lod + cy, z * this->lod + cz, chunkSize)] != 0) {
											lodVals[IDX(x, y, z, this->size)] = chunkData->terrain[IDX(x * this->lod + cx, y * this->lod + cy, z * this->lod + cz, chunkSize)];
											break;
											break;
											break;
										}
									}
								}
							}

						}
					}
				}
				MemFree(chunkData->terrain);

				chunkData->terrain = lodVals;
			}
			chunkData->lod = lod;
		}

	}
	inline void Compress(ChunkData* chunkData) {

		if (voxelCount == 0) {
			return;
		}
		if (chunkData->shortenedData) return;
		chunkData->compressed = false;
		bool over255 = true;
		for (int i = 0; i < size * size * size; i++) {
			if (chunkData->terrain[i] > 255) {
				over255 = false;
				break;
			}
		}
		if (over255) {
			chunkData->compressedTerrain = (uint8_t*)MemAlloc(size * size * size);

			for (int i = 0; i < size * size * size; i++) {
				chunkData->compressedTerrain[i] = (uint8_t)chunkData->terrain[i];
			}

			chunkData->shortenedData = true;

			free(chunkData->terrain);
			chunkData->terrain = NULL;
			return;
		}
		uint16_t voxels[65535];
		for (int i = 0; i < 65535; i++) {
			voxels[i] = 0;
		}
		for (int i = 0; i < size * size * size; i++) {
			voxels[chunkData->terrain[i]] += 1;
		}
		int originalVoxels = 0;
		uint16_t palette[255];
		for (int i = 1; i < 65535; i++) {
			if (voxels[i] != 0) {
				palette[originalVoxels] = i;
				originalVoxels++;
			}
		}
		if (originalVoxels < 255) {
			chunkData->compressed = true;
			chunkData->compressedTerrain = (uint8_t*)MemAlloc(size * size * size);
			uint8_t lookup[65535] = { 0 };
			for (int i = 0; i < originalVoxels; i++) {
				lookup[palette[i]] = i + 1;
			}

			for (int i = 0; i < size * size * size; i++) {
				uint16_t originalValue = chunkData->terrain[i];
				chunkData->compressedTerrain[i] = lookup[originalValue];
			}

			chunkData->paletteSize = originalVoxels;
			chunkData->palette = (uint16_t*)MemAlloc(originalVoxels * sizeof(uint16_t));
			memcpy(chunkData->palette, palette, originalVoxels * sizeof(uint16_t));

			free(chunkData->terrain);
			chunkData->terrain = NULL;
		}
	}
	inline uint16_t* DecompressChunk(ChunkData* chunkData) {
		int totalVoxels = size * size * size;
		uint16_t* decompressedData = (uint16_t*)MemAlloc(totalVoxels * sizeof(uint16_t));
		if (chunkData->compressed) {
			for (int i = 0; i < totalVoxels; i++) {
				uint8_t compressedValue = chunkData->compressedTerrain[i];
				decompressedData[i] = (compressedValue == 0) ? 0 : chunkData->palette[compressedValue - 1];
			}
		}
		else {
			memcpy(decompressedData, chunkData->terrain, totalVoxels * sizeof(uint16_t));
		}

		return decompressedData;
	}
	inline bool SetTerrain(ChunkData* chunkdata, int x, int y, int z, uint16_t t, bool setGPUDATA = true) {
		int id = x + y * size + z * size * size;
		bool set = false;

		if (chunkdata->shortenedData) {
			if (chunkdata->compressedTerrain[id] == 0) {
				voxelCount++;
			}
			chunkdata->compressedTerrain[id] = (uint8_t)t;
			if (setGPUDATA) chunkdata->fluidByteMap.ChangePixel(x, y, z, t);
			//edit byte map
			return true;
		}
		if (chunkdata->compressed) {
			//edit byte map
			uint8_t compressedValue = 0;
			for (int i = 0; i < chunkdata->paletteSize; i++) {
				if (chunkdata->palette[i] == t) {
					compressedValue = i + 1;
					break;
				}
			}
			if (compressedValue == 0 && t != 0) {
				if (chunkdata->paletteSize < 255) {
					chunkdata->palette[chunkdata->paletteSize] = t;
					compressedValue = chunkdata->paletteSize + 1;
					chunkdata->paletteSize++;
				}
				else {
					return false;
				}
			}

			uint8_t oldCompressed = chunkdata->compressedTerrain[id];
			set = oldCompressed != compressedValue;
			if (set) {
				chunkdata->compressedTerrain[id] = compressedValue;
			}
		}
		else {
			//edit short map
			uint16_t oldValue = chunkdata->terrain[id];
			set = oldValue != t;
			if (set) {
				chunkdata->terrain[id] = t;
			}
			if (setGPUDATA) chunkdata->fluidShortMap.ChangePixel(x, y, z, t);
		}

		if (set) {
			voxelCount += set ? 1 : 0;
		}
		return set;
	}
	inline void DownscaleLoD(ChunkData* chunkData, int targetLod)
	{
		if (chunkData->voxelCount == 0) return;

		START_TIMER(downscale_lod)

			while (lod < targetLod) {
				const int srcSize = size;
				const int dstSize = size / 2;
				if (dstSize == 0) break;

				const bool srcIsByte = chunkData->shortenedData || chunkData->compressed;

				auto sampleSource = [&](int sx, int sy, int sz) -> uint16_t {
					int id = sx + sy * srcSize + sz * srcSize * srcSize;
					if (chunkData->shortenedData) {
						return chunkData->compressedTerrain[id];
					}
					if (chunkData->compressed) {
						uint8_t cv = chunkData->compressedTerrain[id];
						return cv == 0 ? 0 : chunkData->palette[cv - 1];
					}
					return chunkData->terrain[id];
					};

				auto downsample = [&](int dx, int dy, int dz) -> uint16_t {
					for (int cz = 0; cz < 2; cz++) {
						int sz = dz * 2 + cz; if (sz >= srcSize) continue;
						for (int cy = 0; cy < 2; cy++) {
							int sy = dy * 2 + cy; if (sy >= srcSize) continue;
							for (int cx = 0; cx < 2; cx++) {
								int sx = dx * 2 + cx; if (sx >= srcSize) continue;
								uint16_t v = sampleSource(sx, sy, sz);
								if (v != 0) return v;
							}
						}
					}
					return 0;
					};

				if (srcIsByte) {
					uint8_t* dst = chunkData->compressedTerrain;

					if (chunkData->compressed) {
						uint8_t lookup[65536];
						memset(lookup, 0, sizeof(lookup));
						for (int i = 0; i < chunkData->paletteSize; i++)
							lookup[chunkData->palette[i]] = (uint8_t)(i + 1);

						std::vector<uint8_t> scratch(dstSize * dstSize * dstSize);
						for (int z = 0; z < dstSize; z++)
							for (int y = 0; y < dstSize; y++)
								for (int x = 0; x < dstSize; x++) {
									uint16_t v = downsample(x, y, z);
									scratch[x + y * dstSize + z * dstSize * dstSize] =
										(v == 0) ? 0 : lookup[v];
								}
						memcpy(dst, scratch.data(), scratch.size());
					}
					else {
						std::vector<uint8_t> scratch(dstSize * dstSize * dstSize);
						for (int z = 0; z < dstSize; z++)
							for (int y = 0; y < dstSize; y++)
								for (int x = 0; x < dstSize; x++)
									scratch[x + y * dstSize + z * dstSize * dstSize] =
									(uint8_t)downsample(x, y, z);
						memcpy(dst, scratch.data(), scratch.size());
					}
				}
				else {
					uint16_t* dst = chunkData->terrain;
					std::vector<uint16_t> scratch(dstSize * dstSize * dstSize);
					for (int z = 0; z < dstSize; z++)
						for (int y = 0; y < dstSize; y++)
							for (int x = 0; x < dstSize; x++)
								scratch[x + y * dstSize + z * dstSize * dstSize] =
								downsample(x, y, z);
					memcpy(dst, scratch.data(), scratch.size() * sizeof(uint16_t));
				}

				size = dstSize;
				lod *= 2;
				chunkData->lod = lod;

				voxelCount = 0;
				const int total = dstSize * dstSize * dstSize;
				if (chunkData->shortenedData || chunkData->compressed) {
					uint8_t* buf = chunkData->compressedTerrain;
					for (int i = 0; i < total; i++) voxelCount += (buf[i] != 0);
				}
				else {
					uint16_t* buf = chunkData->terrain;
					for (int i = 0; i < total; i++) voxelCount += (buf[i] != 0);
				}
				chunkData->voxelCount = voxelCount;
			}

	}
	inline uint32_t PackMetaData(uint8_t mask, uint8_t lod, uint16_t type)
	{
		return
			((uint32_t)(mask & 0x3Fu)) |          // Bits 0-5   (6 bits)
			((uint32_t)(lod & 0x3Fu) << 6) |      // Bits 6-11  (6 bits)
			((uint32_t)(type & 0xFFFFu) << 12);   // Bits 12-27 (16 bits)
	}
	uint16_t ReadVoxel(ChunkData* chunkData, int x, int y, int z) {
		int index = x + y * size + z * size * size;
		if (index<0 || index>size * size * size) return 0;
		if (chunkData->voxelCount == 0) return 0;
		if (chunkData->skipped) return 0;
		if (chunkData->shortenedData) {
			return chunkData->compressedTerrain[index];;

		}
		if (chunkData->compressed) {
			uint8_t compressedValue = chunkData->compressedTerrain[index];
			if (compressedValue == 0) {
				return 0;
			}
			return chunkData->palette[compressedValue - 1];
		}
		else {
			return chunkData->terrain[index];
		}
	}
	inline void ClearChunkData(ChunkData* cd)
	{
		if (!cd) return;
		if (cd->voxelCount != 0) {

			if (cd->terrain && !cd->compressed && cd->lod != 32) {
				MemFree(cd->terrain);
				cd->terrain = NULL;
			}
			if (cd->compressedTerrain && (cd->compressed || cd->shortenedData)) {
				MemFree(cd->compressedTerrain);
				cd->compressedTerrain = NULL;
			}
			if (cd->palette && cd->compressed) {
				MemFree(cd->palette);
				cd->palette = NULL;
			}
		}

		lod = 0;
		voxelCount = 0;
		cd->fluidShortMap.Delete();
		cd->fluidByteMap.Delete();

		cd->skipped = false;
		cd->lod = 0;
		cd->paletteSize = 0;
		cd->compressed = false;
		cd->voxelCount = 0;
		cd->updated = false;

		cd->shortenedData = false;
		updated = false;
		voxelCount = 0;
		allocatedTriSize = 0;
		beginningTri = 0;
		endTri = 0;
		oldTri = 0;

		tallestY = 0;
		grassList.clear();
	}
};

struct World {
	std::vector<AnimatedObject> animatedObjects;
	bool array[mwx][mwz];
	Chunk* chunks;
	uint8_t setLod[mwx][mwz];
	int alreadySpawnedGrass = 0;
	float* particles;
	int totalTriangleCount;
	int totalMeshletCount = 0;
	int workInFrame = 0;
	int grass = 0;
	int waterUpdateFrequency = 4;
	bool waterSimulationEnabled = true;
	int waterResolution = 2;
	CHUNKTYPE selectedType = MOUNTAIN;
	bool simulateWater = false;
	int* containsFluids;
	Math::IVector3* waterSimulationBatch;
	Math::IVector3 currentBatch[1024 * 1024];
	Math::IVector3* softwareRaster;
	int drd = 0;
	struct GlobalHandle {
		struct Region {
			GLuint64 handle = 0;
			GLuint64 lightStorageHandle = 0;
			GLuint64 waterHandle;
			int lod;
			int filled; 
		};
		Region handle[mwx * mwy * mwz];
	};
	int lastBatch = 0;
	bool batchWater = false;
	GlobalHandle handle;
	std::vector<ChunkData> chunkData;
	void Setup() {
		chunkData.resize(wx * wz * wy);
		memset(handle.handle, 0, wx * wy * wz * sizeof(GLuint64));
		for (int x = 0; x < wx; x++) {
			for (int y = 0; y < wz; y++) {
				array[x][y] = false;
				setLod[x][y] = 0;
			}
		}
		softwareRaster = (Math::IVector3*)(MemAlloc(1024 * 1024 * sizeof(Math::IVector3)));
	}
	~World() {
		chunkData.clear();
		chunkData.reserve(0);
	}

	uint16_t ReadVoxel(int x, int y, int z) {
		if (x<0 || y<0 || z < 0 || x>wx * chunkSize || y>wy * chunkSize || z > wz * chunkSize) return 0;

		int chunkX = x / chunkSize;
		int chunkY = y / chunkSize;
		int chunkZ = z / chunkSize;
		int localX = x % chunkSize;
		int localY = y % chunkSize;
		int localZ = z % chunkSize;
		int voxelId = IDX(localX, localY, localZ, chunkSize);
		int chunkId = chunkY * (wx * wz) + chunkZ * wx + chunkX;
		if (chunkData[chunkId].skipped) return 0;
		if (chunkData[chunkId].lod == 32) {
			return 1;
		}
		if (chunkData[chunkId].voxelCount != 0 && chunkData[chunkId].lod != 32 && chunkData[chunkId].lod != 0) {
			if (chunkData[chunkId].shortenedData) {
				return chunkData[chunkId].compressedTerrain[voxelId];
			}
			else if (chunkData[chunkId].compressed) {
				return chunkData[chunkId].palette[voxelId];
			}
			return chunkData[chunkId].terrain[voxelId];
		}
		return 0;
	}
	void CallGPUMesh(bool raytracePrimaryRays, ShaderManager *shaders) {

		if (drd > 0 && !raytracePrimaryRays) {
			shaders->display.UpdateSSBO(10, softwareRaster, drd * sizeof(Math::IVector3), 0);

			auto batchUploadEnd = std::chrono::high_resolution_clock::now();
			Anubis::GpuTimer mesh;
			mesh.Start();
			Anubis::SwitchShaderStage(shaders->generateMesh.programID);
			shaders->generateMesh.SetInt(wx, "wx");
			shaders->generateMesh.SetInt(wy, "wy");
			shaders->generateMesh.SetInt(wz, "wz");
			shaders->generateMesh.Use(drd, 1, 1);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

			constexpr GLintptr N = 1024 * 1024;
			const GLintptr endOffset = sizeof(int) + N * sizeof(int);

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaders->display.ssboIds[9]);

			glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(int), &totalMeshletCount);

			std::vector<int> begin(drd), end(drd);
			glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, sizeof(int),
				drd * sizeof(int), begin.data());
			glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, endOffset,
				drd * sizeof(int), end.data());

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			if (totalMeshletCount * 64 > 59259259) {
				std::cout << "Buffer overflow \n";
			}

			totalTriangleCount = totalMeshletCount * 64 + 64;

			for (int i = 0; i < drd; i++) {


				int id = softwareRaster[i].y * (wx * wz) + softwareRaster[i].z * wx + softwareRaster[i].x;
				chunks[id].beginningTri = begin[i];
				chunks[id].endTri = end[i];
			}
		}
	}
	void RecalculateParticlesFullDebug(ShaderManager *shaders) {

		grass = 0;
		int total = 0;
		int tt = 0;
		for (int x = 0; x < wx; x++) {
			for (int z = 0; z < wz; z++) {
				for (int y = 0; y < wy; y++) {

					tt += chunks[y * (wx * wz) + z * wx + x].grassList.size();
				}
			}
		}
		particles = (float*)MemAlloc(tt * 2 * 3 * sizeof(float));

		for (int x = 0; x < wx; x++) {
			for (int z = 0; z < wz; z++) {
				for (int y = 0; y < wy; y++) {

					for (auto t : chunks[y * (wx * wz) + z * wx + x].grassList) {
						for (int i = 0; i < GetRandomValue(1, 2); i++) {
							particles[total * 3] = (t.x + float(GetRandomValue(-10, 10)) / 10.0f);
							particles[total * 3 + 1] = (t.y + 1);
							particles[total * 3 + 2] = (t.z + float(GetRandomValue(-10, 10)) / 10.0f);
							grass++;
							total++;
						}


					}
					chunks[y * (wx * wz) + z * wx + x].grassList.clear();
				}
			}
		}
		if (grass > 0) {
			std::cout << "grass: " << grass << "\n";
			shaders->display.UpdateSSBO(20, particles, grass * 3 * sizeof(float), 0);
		}
		free(particles);
	}
	inline void uploadChunkSSBO(uint32_t neighbourId, ShaderManager *shaders) {
		Anubis::SwitchShaderStage(shaders->display.shaderId);
		shaders->display.UpdateSSBO(
			3,
			&handle.handle[neighbourId],
			sizeof(World::GlobalHandle::Region),
			neighbourId * sizeof(World::GlobalHandle::Region)
		);
	};
	inline void DeleteWorld(ShaderManager *shaders) {

		for (int nx = 0; nx < wx; nx++) {
			for (int nz = 0; nz < wz; nz++) {

				array[nx][nz] = false;

				for (int ny = 0; ny < wy; ny++) {
					int neighbourId = ny * (wx * wz) + nz * wx + nx;
					chunks[neighbourId].ClearChunkData(&chunkData[neighbourId]);
					chunks[neighbourId].grassList.clear();
					uploadChunkSSBO(neighbourId, shaders);
				}
			}
		}
		free(chunks);
		Anubis::SwitchShaderStage(shaders->display.shaderId);
		memset(shaders->display.ssboPointers[9], 0, (1024 * 1024 * 2 + 1) * sizeof(int));
		memset(shaders->display.ssboPointers[5], 0, (totalMeshletCount + 1) * sizeof(int) * 8);
		memset(shaders->display.ssboPointers[2], 0, totalTriangleCount * sizeof(uint32_t) * 8);
		memset(shaders->display.ssboPointers[6], 0, totalTriangleCount * sizeof(uint32_t) * 8);
		memset(shaders->display.ssboPointers[7], 0, totalMeshletCount * 6 * sizeof(float) * 8);
		shaders->display.UpdateSSBO(20, 0, maxGrassCount * 3 * sizeof(float), 0);
		grass = 0;
	}
	void SimulateFluidObjects(ShaderManager *shaders, Anubis::Camera camera) {
		if (workInFrame == 0) {

			Anubis::SwitchShaderStage(shaders->display.shaderId);

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaders->display.ssboIds[11]);

			int zero = 0;

			glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero);

			glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
		}

		Math::Vector3 offset = { camera.position.x / 32 - 960 / 32,0,camera.position.z / 32 - 960 / 32 };
		int dispatchSize = 960 / 32;

		if (wx < dispatchSize * 2) {
			dispatchSize = wx;
			offset.x = 0;
			offset.z = 0;
		}
		if (lastBatch == 0 || !batchWater) {

			Anubis::SwitchShaderStage(shaders->simulateWater.programID);

			shaders->simulateWater.SetInt(wx, "wx");
			shaders->simulateWater.SetInt(wy, "wy");
			shaders->simulateWater.SetInt(wz, "wz");
			shaders->simulateWater.SetInt(wz, "wz");
			shaders->simulateWater.SetInt(waterResolution, "waterResolution");
			shaders->simulateWater.SetVector3(offset, "cameraOffset");

			shaders->simulateWater.SetInt(workInFrame, "workGroupIDOffset");
			shaders->simulateWater.SetInt(waterUpdateFrequency, "workGroupSpread");

			int totalGroupsX = dispatchSize * 2;

			int groupsPerFrame = (totalGroupsX - workInFrame + waterUpdateFrequency - 1) / waterUpdateFrequency;

			groupsPerFrame = groupsPerFrame > 0 ? groupsPerFrame : 0;

			shaders->simulateWater.Use(groupsPerFrame, wy, dispatchSize * 2);
		}
		else {
			auto flush = [&](int count) {
				Anubis::SwitchShaderStage(shaders->display.shaderId);
				shaders->display.UpdateSSBO(10, currentBatch, count * sizeof(Math::IVector3), 0);

				Anubis::SwitchShaderStage(shaders->simulateWaterBatched.programID);
				shaders->simulateWaterBatched.SetInt(wx, "wx");
				shaders->simulateWaterBatched.SetInt(wy, "wy");
				shaders->simulateWaterBatched.SetInt(wz, "wz");
				shaders->simulateWaterBatched.SetInt(waterResolution, "waterResolution");
				shaders->simulateWaterBatched.Use(count, 1, 1);
				};

			const int maxBatch = 1024 * 1024;
			int n = 0;
			for (int i = workInFrame; i < lastBatch; i += waterUpdateFrequency) {
				currentBatch[n++] = waterSimulationBatch[i];
				if (n == maxBatch) {
					flush(n);
					n = 0;
				}
			}
			if (n > 0) flush(n);
		}
		if (batchWater && workInFrame == waterUpdateFrequency - 1) {


			Anubis::SwitchShaderStage(shaders->display.shaderId);
			glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaders->display.ssboIds[11]);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
				GL_BUFFER_UPDATE_BARRIER_BIT);

			glGetBufferSubData(
				GL_SHADER_STORAGE_BUFFER,
				0,
				wx * wy * wz * sizeof(int) * 2,
				containsFluids
			);

			glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
			int updateAble = 0;
			int shouldUpdate = 0;
			for (int x = 0; x < wx; x++) {
				for (int z = 0; z < wz; z++) {
					if (setLod[x][z] == 1) {
						for (int y = 0; y < wy; y++) {
							int chunkId = y * (wx * wz) + z * wx + x;
							if (containsFluids[chunkId] == 0) {
								chunkData[chunkId].lastChunkWaterActivity += 1;
							}
							else {
								chunkData[chunkId].lastChunkWaterActivity = 0;
							}
							if (chunkData[chunkId].lastChunkWaterActivity < 5) {
								shouldUpdate++;
							}
						}
					}
				}
			}
			if (shouldUpdate < 30 * 30 * wy / 2) {
				int i = 0;
				for (int x = 0; x < wx; x++) {
					for (int z = 0; z < wz; z++) {
						if (setLod[x][z] == 1) {
							for (int y = 0; y < wy; y++) {
								int chunkId = y * (wx * wz) + z * wx + x;
								if (chunkData[chunkId].lastChunkWaterActivity < 5) {
									waterSimulationBatch[i] = { x,y,z };
									i++;
								}
							}
						}
					}
				}

				lastBatch = i;
			}
			else {
				lastBatch = 0;
			}
		}
		workInFrame = (workInFrame + 1) % waterUpdateFrequency;
	}
	inline void ChunkDeleteMeshData(int nx, int ny, int nz, ShaderManager *shaders) {

		int neighbourId = ny * (wx * wz) + nz * wx + nx;

		int beginTri = chunks[neighbourId].beginningTri;
		int endTri = chunks[neighbourId].endTri;
		int triCount = endTri - beginTri;

		if (chunks[neighbourId].voxelCount != 0) {
			memset((uint8_t*)shaders->display.ssboPointers[2] + (size_t)beginTri * sizeof(uint32_t),
				0, (size_t)triCount * sizeof(uint32_t));

			memset((uint8_t*)shaders->display.ssboPointers[6] + (size_t)beginTri * sizeof(uint32_t),
				0, (size_t)triCount * sizeof(uint32_t));

			int beginMeshlet = beginTri / 64;
			int endMeshlet = (endTri + 63) / 64;
			int meshletCount = endMeshlet - beginMeshlet;

			if (meshletCount > 0) {
				memset((uint8_t*)shaders->display.ssboPointers[5] + (size_t)beginMeshlet * sizeof(int),
					0, (size_t)meshletCount * sizeof(int));

				memset((uint8_t*)shaders->display.ssboPointers[7] + (size_t)beginMeshlet * 6 * sizeof(float),
					0, (size_t)meshletCount * 6 * sizeof(float));
			}
		}
	}
	inline void DeleteColumn(int nx, int nz, ShaderManager* shaders) {
		array[nx][nz] = false;

		for (int ny = 0; ny < wy; ny++) {
			int neighbourId = ny * (wx * wz) + nz * wx + nx;
			ChunkDeleteMeshData(nx, ny, nz, shaders);
			chunks[neighbourId].ClearChunkData(&chunkData[neighbourId]);
			chunks[neighbourId].grassList.clear();

			uploadChunkSSBO(neighbourId, shaders);
		}
	}
	inline void RecalculateParticles(int x, int y, int z, ShaderManager *shaders) {
		Anubis::SwitchShaderStage(shaders->display.shaderId);
		int total = 0;
		int tt = chunks[y * (wx * wz) + z * wx + x].grassList.size();
		if (tt <= 0) return;
		particles = (float*)MemAlloc(tt * 2 * 3 * sizeof(float));

		for (auto t : chunks[y * (wx * wz) + z * wx + x].grassList) {
			for (int i = 0; i < Math::GetRandomValueStreamlined(1, 2); i++) {
				particles[total * 3] = (t.x + float(Math::GetRandomValueStreamlined(-10, 10)) / 10.0f);
				particles[total * 3 + 1] = (t.y + 1);
				particles[total * 3 + 2] = (t.z + float(Math::GetRandomValueStreamlined(-10, 10)) / 10.0f);
				grass++;
				total++;
			}
		}

		if (grass > maxGrassCount) grass = maxGrassCount;
		if (grass > 0) {
			shaders->display.UpdateSSBO(20, particles, tt * 3 * sizeof(float), alreadySpawnedGrass * 3 * sizeof(float));
			alreadySpawnedGrass += tt;
		}
		free(particles);
	}
	inline void SetLODChunks(int x, int z, int lod, Anubis::Camera camera, ShaderManager *shaders) {
		for (int y = 0; y < wy; y++) {
			int id = y * (wx * wz) + z * wx + x;

			ChunkDeleteMeshData(x, y, z, shaders);

			chunks[id].ClearChunkData(&chunkData[id]);

			chunkData[id].fluidShortMap.Delete();
			chunkData[id].fluidByteMap.Delete();

			chunks[id].endTri = 0;
			chunks[id].oldTri = 0;
			chunkData[id].updated = false;
		}

		setLod[x][z] = lod;
		array[x][z] = true;

		float dist = Vector2Distance(
			{ (float)x * chunkSize + chunkSize / 2, (float)z * chunkSize + chunkSize / 2 },
			{ float(camera.position.x), float(camera.position.z) });
		bool grass = false;
		if (dist < 1920) grass = true;
		GenerationData gen;
		gen.GenerateMountains(x, z);
#pragma omp parallel for num_threads(4)
		for (int y = gen.lowestChunkY; y < gen.highestChunkY; y++) {
			chunks[y * (wx * wz) + z * wx + x].GeneratePerType(selectedType, &chunkData[y * (wx * wz) + z * wx + x], gen, x, y, z, lod, grass);

			chunks[y * (wx * wz) + z * wx + x].GenerateLod(
				&chunkData[y * (wx * wz) + z * wx + x], x, y, z);
			chunks[y * (wx * wz) + z * wx + x].Compress(
				&chunkData[y * (wx * wz) + z * wx + x]);
		}

		free(gen.heightMap);

		for (int ny = 0; ny < wy; ny++) {
			int neighbourId = ny * (wx * wz) + z * wx + x;
			if (x < 0 || ny < 0 || z < 0 || x >= wx || ny >= wy || z >= wz) continue;
			if (chunks[neighbourId].lod == 0) continue;

			int size = chunkSize / chunks[neighbourId].lod;
			if (chunkData[neighbourId].skipped) {
				handle.handle[neighbourId].lod = 32;
				handle.handle[neighbourId].filled = 1;

				continue;
			}
			if (chunkData[neighbourId].voxelCount != 0 &&
				!chunkData[neighbourId].skipped && chunks[neighbourId].voxelCount > chunkSize * chunkSize * chunkSize) {

				if (chunkData[neighbourId].shortenedData) {
					chunkData[neighbourId].fluidByteMap.Clear({ size, size, size });
					chunkData[neighbourId].fluidByteMap.Upload(
						chunkData[neighbourId].compressedTerrain);
					chunkData[neighbourId].fluidByteMap.GetBindlessHandle();
					handle.handle[neighbourId].handle =
						chunkData[neighbourId].fluidByteMap.handle;
				}
				else {
					chunkData[neighbourId].fluidShortMap.Clear({ size, size, size });
					uint16_t* tt = chunks[neighbourId].DecompressChunk(
						&chunkData[neighbourId]);
					chunkData[neighbourId].fluidShortMap.Upload(tt);
					free(tt);
					chunkData[neighbourId].fluidShortMap.GetBindlessHandle();
					handle.handle[neighbourId].handle =
						chunkData[neighbourId].fluidShortMap.handle;
				}

				handle.handle[neighbourId].lod = chunkData[neighbourId].lod;
			}
		}

	}
};