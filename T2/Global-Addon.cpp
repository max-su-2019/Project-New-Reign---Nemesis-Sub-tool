#include "Global-Addon.h"
#include "Global.h"

#include "src\atomiclock.h"

using namespace std;

extern safeStringMap<string> newID;
extern vector<usize> datapacktracker;
atomic_flag locker = ATOMIC_FLAG_INIT;

void dataIDChange(string& line, usize tracker);

namespace
{
	vector<PatchMapRegistration*>& PatchMaps()
	{
		static vector<PatchMapRegistration*> maps;
		return maps;
	}
}

PatchMapRegistration::PatchMapRegistration(void* map, void (*clear)(void*))
	: map(map), clear(clear)
{
	PatchMaps().push_back(this);
}

PatchMapRegistration::~PatchMapRegistration()
{
	auto& maps = PatchMaps();
	maps.erase(find(maps.begin(), maps.end(), this));
}

void PatchMapRegistration::ClearAll()
{
	for (const auto registration : PatchMaps())
	{
		registration->clear(registration->map);
	}
}

void NemesisReaderFormat(int id, vecstr& output)
{
	usize tracker = 0;
	
	if (id < 90000)
	{
		for (auto& dataid : datapacktracker)
		{
			if (id > int(dataid)) ++tracker;
		}
	}

	for (auto& line : output)
	{
		if (line.find("#", 0) != string::npos)
		{
			usize tempint = 0;
			usize position = 0;
			usize size = count(line.begin(), line.end(), '#');

			for (unsigned int j = 0; j < size; ++j)
			{
				position = line.find("#", tempint);
				tempint = line.find("#", position + 1);
				string tempID;

				tempID = "#";

				for (usize i = position + 1; i < line.length(); ++i)
				{
					if (isdigit(line[i])) tempID.push_back(line[i]);
					else break;
				}

				int intID = stoi(tempID.substr(1));

				if (intID > 89999)
				{
					int position2 = line.find(tempID);
					string modID = NodeIDCheck(tempID);
					line.replace(position2, tempID.length(), modID);
				}
			}
		}

		//dataIDChange(line, tracker);
	}
}

void dataIDChange(string& line, usize tracker)
{
	if (line.find("<hkparam name=\"id\">", 0) != string::npos && line.find("<hkparam name=\"id\">-1</hkparam>", 0) == string::npos)
	{
		usize eventpos = line.find("id\">") + 4;
		string eventid;

		for (usize i = eventpos; i < line.length(); ++i)
		{
			if (isdigit(line[i])) eventid.push_back(line[i]);
			else break;
		}

		if (eventid.length() > 0 && stoi(eventid) >= int(eventCount[tracker]) && eventID[tracker][eventid].length() != 0)
		{
			line.replace(eventpos, eventid.length(), "$eventID[" + eventID[tracker][eventid] + "]$");
		}
	}

	{
		string lowerline = boost::to_lower_copy(line);

		if (lowerline.find("eventid\">", 0) != string::npos && lowerline.find("eventid\">-1</hkparam>", 0) == string::npos)
		{
			usize eventpos = lowerline.find("eventid\">") + 9;
			string eventid;

			for (usize i = eventpos; i < line.length(); ++i)
			{
				if (isdigit(line[i])) eventid.push_back(line[i]);
				else break;
			}

			if (eventid.length() > 0 && stoi(eventid) >= int(eventCount[tracker]) && eventID[tracker][eventid].length() != 0)
			{
				line.replace(eventpos, eventid.length(), "$eventID[" + eventID[tracker][eventid] + "]$");
			}
		}
	}

	if (line.find("<hkparam name=\"variableIndex\">", 0) != string::npos)
	{
		usize varpos = line.find("<hkparam name=\"variableIndex\">") + 30;
		string varid;

		for (usize i = varpos; i < line.length(); ++i)
		{
			if (isdigit(line[i])) varid.push_back(line[i]);
			else break;
		}

		if (varid.length() > 0 && stoi(varid) >= int(varCount[tracker]) && variableID[tracker][varid].length() != 0)
		{
			line.replace(varpos, varid.length(), "$variableID[" + variableID[tracker][varid] + "]$");
		}
	}
}

vecstr GetElements(string number, unordered_map<string, vecstr>& functionlines, bool isTransition, string key)
{
	vecstr elements;
	vecstr storeline = functionlines[number];

	if (isTransition)
	{
		for (auto& line : storeline)
		{
			size_t pos = line.find("name=\"" + key + "\">", 0);

			if (pos != string::npos)
			{
				if (line.find("#", pos) != string::npos)
				{
					size_t tempInt = line.find("#");
					string reference = line.substr(tempInt, line.find("</hkparam>", tempInt) - tempInt);
					elements.push_back(reference);
				}
			}
		}
	}
	else
	{
		bool activate = false;

		for (auto& line : storeline)
		{
			if (!activate)
			{
				if (line.find("numelements=\"") != string::npos)
				{
					activate = true;
				}
			}
			else
			{
				if (line.find("</hkparam>") != string::npos)
				{
					break;
				}

				stringstream sstream(line);
				istream_iterator<string> ssbegin(sstream);
				istream_iterator<string> ssend;
				vecstr curElements(ssbegin, ssend);
				copy(curElements.begin(), curElements.end(), curElements.begin());

				for (auto& element : curElements)
				{
					if (element.length() == 0 || element[0] != '#')
					{
						PatchError{} << "ERROR: Invalid element. (Element: " << element << ")" << endl;
						return elements;
					}
					else
					{
						elements.push_back(element);
					}
				}
			}
		}
	}

	return elements;
}

string NodeIDCheck(string ID)
{
	nodelock lock(&atomLock);
	string modID;

	if (!newID[ID].empty())
	{
		modID = newID[ID];
	}
	else
	{
		modID = "#" + modcode + "$" + to_string(functioncount++);
		newID[ID] = modID;
	}

	return modID;
}

bool isOnlyNumber(string line)
{
	try
	{
		boost::lexical_cast<double>(line);
	}
	catch (boost::bad_lexical_cast &)
	{
		return false;
	}

	return true;
}

bool hasAlpha(string line)
{
	string lower = boost::to_lower_copy(line);
	string upper = boost::to_upper_copy(line);

	if (lower != upper)
	{
		return true;
	}

	return false;
}

