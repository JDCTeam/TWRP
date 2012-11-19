#ifndef MULTIROM_H
#define MULTIROM_H

#include <string>
#include <sys/stat.h>

enum
{
    ROM_DEFAULT           = 0,
    ROM_ANDROID_INTERNAL  = 1,
    ROM_UBUNTU_INTERNAL   = 2,
    ROM_ANDROID_USB       = 3,
    ROM_UBUNTU_USB        = 4,

    ROM_UNKNOWN           = 5
};

class MultiROM
{
public:
	static bool folderExists();
	static std::string getRomsPath();
	static int getType(std::string name);

	static bool move(std::string from, std::string to);
	static bool erase(std::string name);

	static bool flashZip(std::string rom, std::string file);

private:
	static void findPath();

	static std::string m_path;
};

std::string MultiROM::m_path = "";

bool MultiROM::folderExists()
{
	findPath();
	return !m_path.empty();
}

std::string MultiROM::getRomsPath()
{
	return m_path + "/roms/";
}

void MultiROM::findPath()
{
	static const char *paths[] = {
		"/data/media/multirom",
		"/data/media/0/multirom",
		NULL
	};

	struct stat info;
	for(int i = 0; paths[i]; ++i)
	{
		if(stat(paths[i], &info) >= 0)
		{
			m_path = paths[i];
			return;
		}
	}
	m_path.clear();
}

bool MultiROM::move(std::string from, std::string to)
{
	std::string roms = getRomsPath();
	std::string cmd = "mv \"" + roms + "/" + from + "\" ";
	cmd += "\"" + roms + "/" + to + "\"";
	return system(cmd.c_str()) == 0;
}

bool MultiROM::erase(std::string name)
{
	std::string roms = getRomsPath();
	std::string cmd = "rm -rf \"" + roms + "/" + name + "\"";
	return system(cmd.c_str()) == 0;
}

int MultiROM::getType(std::string name)
{
	std::string path = getRomsPath() + "/" + name + "/";
	struct stat info;
	
	if (stat((path + "system").c_str(), &info) >= 0 &&
		stat((path + "data").c_str(), &info) >= 0 &&
		stat((path + "cache").c_str(), &info) >= 0)
		return ROM_ANDROID_INTERNAL;

	if(stat((path + "root").c_str(), &info) >= 0)
		return ROM_UBUNTU_INTERNAL;
	return ROM_UNKNOWN;
}

bool MultiROM::flashZip(std::string rom, std::string file)
{
	
}

#endif
