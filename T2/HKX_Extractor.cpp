#include "HKX_Extractor.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/process/system.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "src/FillFunction.h"
#include "AnimData/animdatacore.h"
#include "AnimSetData/animsetdatacore.h"

namespace fs = boost::filesystem;

namespace
{
	std::mutex patchMutex;

	struct PatchState
	{
		~PatchState() { ResetPatchState(); }
	};

	struct PatchWorkspace
	{
		fs::path directory;
		bool keepBackups = false;

		explicit PatchWorkspace(const fs::path& output)
			: directory(output / fs::unique_path(L".hkx-extractor-%%%%-%%%%-%%%%-%%%%"))
		{
			if (!fs::create_directory(directory))
			{
				throw std::runtime_error("Unable to create a patch workspace");
			}
		}

		~PatchWorkspace()
		{
			if (!keepBackups)
			{
				boost::system::error_code ignored;
				fs::remove_all(directory, ignored);
			}
		}
	};

	enum class InputType { unknown, behavior, animData, animSetData };

	struct PatchInput
	{
		vecstr lines;
		InputType type = InputType::unknown;
	};

	std::string NormalizeModCode(const char* modCode)
	{
		std::string code(modCode);
		if (code.size() < 4 || code.size() > 6)
		{
			throw std::invalid_argument("Expected a mod code of 4 to 6 ASCII letters or digits");
		}
		for (auto& ch : code)
		{
			if (ch >= 'A' && ch <= 'Z')
			{
				ch += 'a' - 'A';
			}
			if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9'))
			{
				throw std::invalid_argument("Invalid mod code");
			}
		}
		return code;
	}

	PatchInput ReadInput(const fs::path& path)
	{
		fs::ifstream file(path, std::ios::binary);
		if (!file)
		{
			throw std::runtime_error("Unable to open patch input");
		}

		PatchInput input;
		for (std::string line; std::getline(file, line);)
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			input.lines.push_back(std::move(line));
		}
		if (file.bad())
		{
			throw std::runtime_error("Unable to read patch input");
		}

		if (!input.lines.empty() && input.lines.front().compare(0, 3, "\xef\xbb\xbf") == 0)
		{
			input.lines.front().erase(0, 3);
		}
		for (size_t i = 0; i < input.lines.size() && i < 4; ++i)
		{
			if (input.lines[i].find("<hkpackfile") != std::string::npos)
			{
				input.type = InputType::behavior;
				return input;
			}
		}

		if (input.lines.size() >= 2 && !input.lines[0].empty()
			&& input.lines[0].find_first_not_of("0123456789") == std::string::npos
			&& std::stoull(input.lines[0]) < input.lines.size())
		{
			const auto& firstProject = input.lines[1];
			if (boost::ends_with(firstProject, ".txt"))
			{
				input.type = firstProject.find('\\') == std::string::npos
					? InputType::animData : InputType::animSetData;
			}
		}
		return input;
	}

	fs::path FindConverter()
	{
		HMODULE module = nullptr;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
			| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&GenerateBehaviorPatchFiles), &module))
		{
			std::wstring dllPath(32768, L'\0');
			const DWORD length = GetModuleFileNameW(module, &dllPath[0], static_cast<DWORD>(dllPath.size()));
			if (length > 0 && length < dllPath.size())
			{
				dllPath.resize(length);
				const auto converter = fs::path(dllPath).parent_path() / L"hkxcmd.exe";
				if (fs::is_regular_file(converter))
				{
					return converter;
				}
			}
		}

		const auto converter = fs::absolute(L"hkxcmd.exe");
		if (!fs::is_regular_file(converter))
		{
			throw std::runtime_error("Unable to locate hkxcmd.exe");
		}
		return converter;
	}

	PatchInput LoadInput(const fs::path& source, const fs::path& converted)
	{
		auto input = ReadInput(source);
		if (input.type != InputType::unknown || !boost::iequals(source.extension().native(), L".hkx"))
		{
			return input;
		}

		const auto converter = FindConverter();
		if (boost::process::system(converter, L"convert", L"-v:XML", source.native(), converted.native()) != 0)
		{
			throw std::runtime_error("HKX conversion failed");
		}
		return ReadInput(converted);
	}

	std::string ReadBehaviorRoot(const vecstr& lines)
	{
		std::stringstream xml;
		for (const auto& line : lines)
		{
			xml << line << '\n';
		}
		boost::property_tree::ptree document;
		boost::property_tree::read_xml(xml, document);
		const auto& packfile = document.get_child("hkpackfile");
		const auto root = packfile.get<std::string>("<xmlattr>.toplevelobject");
		if (root.size() < 2 || root[0] != '#' || root.find_first_not_of("0123456789", 1) != std::string::npos)
		{
			throw std::runtime_error("Invalid Behavior root node");
		}
		for (const auto& section : packfile)
		{
			if (section.first != "hksection")
			{
				continue;
			}
			for (const auto& object : section.second)
			{
				if (object.first == "hkobject" && object.second.get("<xmlattr>.name", "") == root)
				{
					return root;
				}
			}
		}
		throw std::runtime_error("Missing Behavior root object");
	}

	void ConnectBehavior(const std::string& source, const std::string& root, bool edited)
	{
		auto node = edited ? editedBehavior["#9" + root.substr(1)] : originalBehavior[root];
		if (!node)
		{
			throw std::runtime_error("Missing Behavior root node");
		}
		node->connect(source, "root=", 0, edited, nullptr);
	}

	void CompareNode(const std::string& id, const std::shared_ptr<hkbobject>& edited)
	{
		auto original = originalBehavior[id];
		if (original)
		{
			original->match(edited);
		}
		else
		{
			edited->newNode();
		}
	}

	void GenerateBehavior(PatchInput& original, PatchInput& edited)
	{
		const auto originalRoot = ReadBehaviorRoot(original.lines);
		const auto editedRoot = ReadBehaviorRoot(edited.lines);
		AddBehavior(targetfilename, original.lines, false);
		AddBehavior(targetfilenameedited, edited.lines, true);
		if (Error)
		{
			return;
		}

		num_thread = (std::numeric_limits<unsigned int>::max)();
		ConnectBehavior(targetfilename, originalRoot, false);
		idcount.clear();
		IsExist.clear();
		elements.clear();
		exchangeID.clear();
		referencingIDs.clear();
		if (Error)
		{
			return;
		}
		ConnectBehavior(targetfilenameedited, editedRoot, true);
		if (Error)
		{
			return;
		}

		for (const auto& node : editedBehavior)
		{
			if (node.second && !originalBehavior[node.first])
			{
				NodeIDCheck(node.second->ID);
			}
		}
		for (const auto& node : hkbbehaviorgraphstringdataList_E)
		{
			CompareNode(node.first, node.second);
		}
		for (const auto& node : hkbcharacterstringdataList_E)
		{
			CompareNode(node.first, node.second);
		}
		for (const auto& node : editedBehavior)
		{
			if (node.second && node.second->getClassCode() != "b" && node.second->getClassCode() != "cc")
			{
				CompareNode(node.first, node.second);
			}
			if (Error)
			{
				return;
			}
		}
	}

	void CheckDestination(const fs::path& path)
	{
		const auto attributes = GetFileAttributesW(path.c_str());
		if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
		{
			throw std::runtime_error("Patch output contains a reparse point");
		}
	}

	struct PatchFile
	{
		fs::path source;
		fs::path destination;
		fs::path backup;
	};

	void PublishPatches(PatchWorkspace& workspace, const fs::path& source, const fs::path& output,
		const fs::path& original, const fs::path& edited)
	{
		std::vector<PatchFile> files;
		for (const auto& entry : fs::recursive_directory_iterator(source))
		{
			if (!fs::is_regular_file(entry))
			{
				continue;
			}

			const auto relative = entry.path().lexically_relative(source);
			auto parent = output;
			for (const auto& part : relative.parent_path())
			{
				parent /= part;
				CheckDestination(parent);
				fs::create_directory(parent);
			}
			const auto destination = output / relative;
			CheckDestination(destination);
			PatchFile file{entry.path(), destination, {}};
			if (fs::exists(destination))
			{
				if (!fs::is_regular_file(destination) || fs::equivalent(destination, original)
					|| fs::equivalent(destination, edited))
				{
					throw std::runtime_error("Patch output collides with an input or directory");
				}
				file.backup = workspace.directory / std::to_wstring(files.size());
				fs::copy_file(destination, file.backup);
			}
			files.push_back(std::move(file));
		}

		size_t installed = 0;
		try
		{
			for (const auto& file : files)
			{
				if (!MoveFileExW(file.source.c_str(), file.destination.c_str(), MOVEFILE_REPLACE_EXISTING))
				{
					throw std::runtime_error("Unable to publish patch file");
				}
				++installed;
			}
		}
		catch (...)
		{
			while (installed > 0)
			{
				const auto& file = files[--installed];
				const BOOL restored = file.backup.empty()
					? DeleteFileW(file.destination.c_str())
					: MoveFileExW(file.backup.c_str(), file.destination.c_str(), MOVEFILE_REPLACE_EXISTING);
				workspace.keepBackups = workspace.keepBackups || !restored;
			}
			throw;
		}
	}
}

extern "C" bool __cdecl GenerateBehaviorPatchFiles(
	const char* modCode, const char* originalFile, const char* editedFile, const char* outputPath)
{
	if (!modCode || !originalFile || !editedFile || !outputPath
		|| !*modCode || !*originalFile || !*editedFile || !*outputPath)
	{
		return false;
	}

	try
	{
		const std::lock_guard<std::mutex> lock(patchMutex);
		const PatchState state;
		modcode = NormalizeModCode(modCode);
		const auto originalPath = fs::absolute(PathFromUtf8(originalFile));
		const auto editedPath = fs::absolute(PathFromUtf8(editedFile));
		const auto output = fs::absolute(PathFromUtf8(outputPath));
		if (!fs::is_regular_file(originalPath) || !fs::is_regular_file(editedPath))
		{
			return false;
		}

		fs::create_directories(output);
		PatchWorkspace workspace(output);
		auto original = LoadInput(originalPath, workspace.directory / L"original.hkx");
		auto edited = LoadInput(editedPath, workspace.directory / L"edited.hkx");
		if (original.type == InputType::unknown || original.type != edited.type)
		{
			return false;
		}
		if (original.lines == edited.lines)
		{
			if (original.type == InputType::behavior)
			{
				ReadBehaviorRoot(original.lines);
			}
			return true;
		}

		targetfilename = originalFile;
		targetfilenameedited = editedFile;
		shortFileName = "behavior";
		patchWorkspace = workspace.directory / L"patches";
		const auto patchRoot = "mod/" + modcode + "/";
		const auto folder = original.type == InputType::behavior ? shortFileName
			: original.type == InputType::animData ? "animationdatasinglefile" : "animationsetdatasinglefile";
		const auto source = PatchPath(patchRoot + folder);
		fs::create_directories(source);

		switch (original.type)
		{
		case InputType::behavior:
			GenerateBehavior(original, edited);
			break;
		case InputType::animData:
			AddAnimData(targetfilename, original.lines, false);
			if (!Error) AddAnimData(targetfilenameedited, edited.lines, true);
			if (!Error) animDataProcess();
			break;
		case InputType::animSetData:
			AddAnimSetData(targetfilename, original.lines, false);
			if (!Error) AddAnimSetData(targetfilenameedited, edited.lines, true);
			if (!Error) animSetDataProcess();
			break;
		default:
			return false;
		}
		if (Error)
		{
			return false;
		}

		PublishPatches(workspace, source, output, originalPath, editedPath);
		return true;
	}
	catch (const std::exception& error)
	{
		OutputDebugStringA(error.what());
		return false;
	}
	catch (...)
	{
		return false;
	}
}
