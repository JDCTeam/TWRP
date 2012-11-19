#ifndef MULTIROM_H
#define MULTIROM_H

#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>

enum
{
    ROM_DEFAULT           = 0,
    ROM_ANDROID_INTERNAL  = 1,
    ROM_UBUNTU_INTERNAL   = 2,
    ROM_ANDROID_USB       = 3,
    ROM_UBUNTU_USB        = 4,

    ROM_UNKNOWN           = 5
};

#define INTERNAL_NAME "Internal"

class MultiROM
{
public:
	struct config {
		config() {
			is_second_boot = 0;
			current_rom = INTERNAL_NAME;
			auto_boot_seconds = 5;
			auto_boot_rom = INTERNAL_NAME;
		}

		int is_second_boot;
		std::string current_rom;
		int auto_boot_seconds;
		std::string auto_boot_rom;
	};

	static bool folderExists();
	static std::string getRomsPath();
	static int getType(std::string name);
	static std::string listRoms();

	static bool move(std::string from, std::string to);
	static bool erase(std::string name);

	static bool flashZip(std::string rom, std::string file);

	static config loadConfig();
	static void saveConfig(const config& cfg);

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

static bool rom_sort(std::string a, std::string b)
{
	if(a == INTERNAL_NAME)
		return true;
	if(b == INTERNAL_NAME)
		return false;
	return a.compare(b) > 0;
}

std::string MultiROM::listRoms()
{
	DIR *d = opendir(getRomsPath().c_str());
	if(!d)
		return "";

	std::vector<std::string> vec;
	struct dirent *dr;
	while((dr = readdir(d)) != NULL)
	{
		if(dr->d_type != DT_DIR)
			continue;

		if(dr->d_name[0] == '.')
			continue;

		vec.push_back(dr->d_name);
	}
	closedir(d);

	std::sort(vec.begin(), vec.end(), rom_sort);

	std::string res = "";
	for(size_t i = 0; i < vec.size(); ++i)
		res += vec[i] + "\n";
	return res;
}

MultiROM::config MultiROM::loadConfig()
{
	config cfg;

	FILE *f = fopen((m_path + "/multirom.ini").c_str(), "r");
	if(f)
	{
		char line[512];
		char *p;
		std::string name, val;
		while(fgets(line, sizeof(line), f))
		{
			p = strtok(line, "=\n");
			if(!p)
				continue;
			name = p;

			p = strtok(NULL, "=\n");
			if(!p)
				continue;
			val = p;

			if(name == "is_second_boot")
				cfg.is_second_boot = atoi(val.c_str());
			else if(name == "current_rom")
				cfg.current_rom = val;
			else if(name == "auto_boot_seconds")
				cfg.auto_boot_seconds = atoi(val.c_str());
			else if(name == "auto_boot_rom")
				cfg.auto_boot_rom = val;
		}
		fclose(f);
	}
	return cfg;
}

void MultiROM::saveConfig(const MultiROM::config& cfg)
{
	FILE *f = fopen((m_path + "/multirom.ini").c_str(), "w");
	if(!f)
		return;

	fprintf(f, "is_second_boot=%d\n", cfg.is_second_boot);
	fprintf(f, "current_rom=%s\n", cfg.current_rom.c_str());
	fprintf(f, "auto_boot_seconds=%d\n", cfg.auto_boot_seconds);
	fprintf(f, "auto_boot_rom=%s\n", cfg.auto_boot_rom.c_str());

	fclose(f);
}

bool MultiROM::flashZip(std::string rom, std::string file)
{
	return false;
}

#endif
