#ifndef MULTIROM_H
#define MULTIROM_H

#include <string>
#include <sys/stat.h>

class MultiROM
{
public:
	static bool folderExists();
	static std::string getRomsPath();

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

#endif