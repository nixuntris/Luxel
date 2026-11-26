#pragma once
#include <vector>
#include <iostream>
#include "Anubis/AnubisMath.hpp"
#include "pugixml.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <sstream>
class VoxFileManager {
	struct VoxItem {
		std::string file, object, name;
		Math::Vector3 pos, rot, size;
	};
	//This format is actually terrible, it's so terrible in fact I am writing a manager class for it and not connecting it to my structure class
	//I'll keep it as simple as possible, and as ripe-able out as possible for other people. 
	//If you don't need to write a parser, don't.
	//I'll try to write a palette globalizer, as I know many people want that and the models store their own. Currently it's only voxel positions.
	//It also aims to support loading teardown maps 
public:
	std::map<std::string, std::vector<Math::IVector3>> voxelData;
	static const char* GetAttribute(pugi::xml_node n, const char* name) {
		if (n.attribute(name)) {
			return n.attribute(name).value();
		}
		return "";
	}
	static Math::Vector3 ReadVector3(pugi::xml_node n, const char* attr) {
		Math::Vector3 vector = { -1,-1,-1 };
		pugi::xml_attribute attribute = n.attribute(attr);
		if (!attribute) return vector;
		std::istringstream ss(attribute.value());
		ss >> vector.x >> vector.y >> vector.z;
		return vector;
	}
	static std::vector<VoxItem> CollectVox(pugi::xml_node n) {
		std::vector<pugi::xml_node> stack;
		stack.push_back(n);
		std::vector<VoxItem> out;
		while (!stack.empty()) {
			pugi::xml_node node = stack.back();
			stack.pop_back();

			for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
				if (c.type() != pugi::node_element) continue;

				if (strcmp(c.name(), "vox") == 0) {
					VoxItem v;
					v.file = GetAttribute(c, "file");
					v.object = GetAttribute(c, "object");
					v.name = GetAttribute(c, "name");
					v.pos = ReadVector3(c, "pos");
					v.rot = ReadVector3(c, "rot");
					v.size = ReadVector3(c, "size");
					out.push_back(v);
				}

				stack.push_back(c);
			}
		}
		return out;
	}
	static void CollectLights(const pugi::xml_node& n, std::vector<pugi::xml_node>& out) {
		pugi::xml_node element = n.first_child();

		while (element) {
			if (element.type() == pugi::node_element) {
				if (std::string(element.name()) == "light") {
					out.push_back(element);
				}
				if (element.first_child()) {
					element = element.first_child();
					continue;
				}
			}

			while (element && element != n && !element.next_sibling()) {
				element = element.parent();
			}
			if (element == n) { 
				break; 
			}
			element = element.next_sibling();
		}
	}
	bool LoadVOX(const std::string& path, std::vector<Math::IVector3>& voxels)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file) return false;

		char magic[4];
		if (!file.read(magic, 4) || std::memcmp(magic, "VOX ", 4) != 0) {
			return false;
		}
		uint32_t version = 0;
		if (!file.read(reinterpret_cast<char*>(&version), 4)) return false;

		char mainId[4];
		uint32_t mainContent = 0, mainChildren = 0;
		if (!file.read(mainId, 4)) return false;
		if (!file.read(reinterpret_cast<char*>(&mainContent), 4)) return false;
		if (!file.read(reinterpret_cast<char*>(&mainChildren), 4)) return false;

		std::streampos end = file.tellg()
			+ (std::streamoff)(mainContent)
			+ (std::streamoff)(mainChildren);

		while (file.tellg() < end)
		{
			char id[4];
			uint32_t content = 0, children = 0;

			file.read(id, 4);
			if (!file) break;
			file.read(reinterpret_cast<char*>(&content), 4);
			file.read(reinterpret_cast<char*>(&children), 4);
			if (!file) break;

			std::streampos contentPos = file.tellg();
			std::streampos next = contentPos
				+ static_cast<std::streamoff>(content)
				+ static_cast<std::streamoff>(children);

			std::string cid(id, 4);

			if (cid == "XYZI")
			{
				uint32_t count = 0;
				file.read(reinterpret_cast<char*>(&count), 4);
				if (!file) return false;

				voxels.reserve(voxels.size() + count);
				for (uint32_t i = 0; i < count; ++i)
				{
					uint8_t x, y, z, c;
					if (!file.read(reinterpret_cast<char*>(&x), 1) ||
						!file.read(reinterpret_cast<char*>(&y), 1) ||
						!file.read(reinterpret_cast<char*>(&z), 1) ||
						!file.read(reinterpret_cast<char*>(&c), 1))
						return false;

					Math::IVector3 v;
					v.x = x; v.y = y; v.z = z;
					voxels.push_back(v);
				}
			}

			file.seekg(next);
		}

		return !voxels.empty();
	}
	std::vector<std::string> SplitPath(const std::string& path) {
		std::vector<std::string> parts;
		size_t start = 0;
		while (start <= path.size()) {
			size_t slash = path.find('/', start);
			if (slash == std::string::npos) {
				if (start < path.size())
					parts.emplace_back(path, start);
				break;
			}
			if (slash > start) 
				parts.emplace_back(path, start, slash - start);
			start = slash + 1;
		}
		return parts;
	}

	void LoadLevel(std::string startpath) {

		std::string XMLlocation = startpath + "/main.xml";

		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file(XMLlocation.c_str());
		if (!result)
			return;

		pugi::xml_node scene = doc.child("scene");
		Math::Vector3  rot = ReadVector3(scene, "rot");
		std::vector<VoxItem> voxes = CollectVox(scene);
		std::map<std::string, std::vector<VoxItem>> objectTypeCount;
		for (auto& v : voxes) {
			std::vector<std::string> path = SplitPath(v.file);
			bool building = false;
			for (auto t : path) {
				if (t == "building") building = true;
			}
			if (building) {
				std::string filePath = "";
				if (path[1] != "assets") {
					filePath += "assets/";
				}
				for (int i = 1; i < path.size(); i++) {
					filePath += path[i];
					if (i != path.size() - 1) {
						filePath += "/";
					}
				}
				objectTypeCount[filePath].push_back(v);
			}
		}
		for (const auto& kv : objectTypeCount) {
			std::string completePath = startpath +"/" + kv.first;
			if (!LoadVOX(completePath, voxelData[kv.first])) {
				std::cerr << "Failed to parse: " << completePath << "\n";
			}
			else {
				std::cout << completePath << " -> " << voxelData[kv.first].size() << " voxels\n";
			}
			for (const auto object : objectTypeCount[kv.first]) {

				//std::cout << object.file << " " << object.name << " " << object.pos.x << " " << object.pos.y << " " << object.pos.z << " " << object.rot.x << " " << object.rot.y << " " << object.rot.z << "\n";
			}
		}
		/*
		std::vector<pugi::xml_node> lights;
		CollectLights(scene, lights);
		std::cout << "Found " << lights.size() << " light sources\n";
		for (auto& l : lights) {
			std::cout << "  type=" << GetAttribute(l, "type") << " color=" << GetAttribute(l, "color") << " scale=" << GetAttribute(l, "scale") << "\n";
		}*/
	}

};
