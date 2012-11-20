#ifndef MULTIROM_H
#define MULTIROM_H

#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <vector>
#include <errno.h>
#include <sys/mount.h>

#include "twinstall.h"
#include "minzip/Zip.h"
#include "roots.h"

enum { INSTALL_SUCCESS, INSTALL_ERROR, INSTALL_CORRUPT };

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
#define REALDATA "/realdata"

// Not defined in android includes?
#define MS_RELATIME (1<<21)

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

	struct file_backup {
		std::string name;
		char *content;
		int size;
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
	static bool changeMounts(std::string base);
	static void restoreMounts();
	static bool prepareZIP(std::string& file);
	static bool skipLine(const char *line);
	
	static std::string m_path;
	static std::vector<file_backup> m_mount_bak;
};

std::string MultiROM::m_path = "";
std::vector<MultiROM::file_backup> MultiROM::m_mount_bak;

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

bool MultiROM::changeMounts(std::string base)
{
	mkdir(REALDATA, 0777);
	if(mount("/dev/block/platform/sdhci-tegra.3/by-name/UDA",
		    REALDATA, "ext4", MS_RELATIME | MS_NOATIME,
            "user_xattr,acl,barrier=1,data=ordered") < 0)
	{
		ui_print("Failed to mount realdata: %d (%s)", errno, strerror(errno));
		return false;
	}

	base.replace(0, 5, REALDATA);

	static const char *files[] = {
		"/etc/fstab",
		"/etc/recovery.fstab",
		NULL
	};

	for(size_t i = 0; i < m_mount_bak.size(); ++i)
		delete[] m_mount_bak[i].content;
	m_mount_bak.clear();

	for(int i = 0; files[i]; ++i)
	{
		FILE *f = fopen(files[i], "r");
		if(!f)
			return false;

		fseek(f, 0, SEEK_END);
		int size = ftell(f);
		rewind(f);

		file_backup b;
		b.name = files[i];
		b.size = size;
		b.content = new char[size]();
		fread(b.content, 1, size, f);
		fclose(f);

		m_mount_bak.push_back(b);
	}
	system("umount /system /data /cache");

	FILE *f_fstab = fopen("/etc/fstab", "w");
	if(!f_fstab)
		return false;

	FILE *f_rec = fopen("/etc/recovery.fstab", "w");
	if(!f_rec)
	{
		fclose(f_fstab);
		return false;
	}

	fprintf(f_rec, "# mount point\tfstype\t\tdevice\n");
	fprintf(f_rec, "/system\t\text4\t\t%s/system\n", base.c_str()); 
	fprintf(f_rec, "/cache\t\text4\t\t%s/cache\n", base.c_str());
	fprintf(f_rec, "/data\t\text4\t\t%s/data\n", base.c_str());
	fprintf(f_rec, "/misc\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/MSC\n");
	fprintf(f_rec, "/boot\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/LNX\n");
	fprintf(f_rec, "/recovery\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/SOS\n");
	fprintf(f_rec, "/staging\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/USP\n");
	fprintf(f_rec, "/usb-otg\t\tvfat\t\t/dev/block/sda1\n");
	fclose(f_rec);

	fprintf(f_fstab, "%s/system /system ext4 rw,bind\n", base.c_str());
	fprintf(f_fstab, "%s/cache /cache ext4 rw,bind\n", base.c_str());
	fprintf(f_fstab, "%s/data /data ext4 rw,bind\n", base.c_str());
	fprintf(f_fstab, "/usb-otg vfat rw\n");
	fclose(f_fstab);

	system("mount /system");
	system("mount /data");
	system("mount /cache");

	load_volume_table();
	return true;
}

void MultiROM::restoreMounts()
{
	system("umount /system /data /cache");

	for(size_t i = 0; i < m_mount_bak.size(); ++i)
	{
		file_backup &b = m_mount_bak[i];
		FILE *f = fopen(b.name.c_str(), "w");
		if(f)
		{
			fwrite(b.content, 1, b.size, f);
			fclose(f);
		}
		delete[] b.content;
	}
	m_mount_bak.clear();

	system("umount "REALDATA);
	load_volume_table();
	system("mount /data");
}

#define MR_UPDATE_SCRIPT_PATH  "META-INF/com/google/android/"
#define MR_UPDATE_SCRIPT_NAME  "META-INF/com/google/android/updater-script"

bool MultiROM::flashZip(std::string rom, std::string file)
{
	ui_print("Flashing ZIP file %s\n", file.c_str());
	ui_print("ROM: %s\n", rom.c_str());

	if(!changeMounts(getRomsPath() + "/" + rom))
	{
		ui_print("Failed to change mountpoints!\n");
		return false;
	}

	ui_print("Preparing ZIP file...\n");
	if(!prepareZIP(file))
		return false;

	int wipe_cache = 0;
	ui_print("1");
	int status = TWinstall_zip(file.c_str(), &wipe_cache);
	ui_print("2");

	system("rm -r "MR_UPDATE_SCRIPT_PATH);
	system("rm /tmp/mr_update.zip");

	if(status != INSTALL_SUCCESS)
		ui_print("Failed to install ZIP!\n");
	else
		ui_print("ZIP successfully installed\n");

	restoreMounts();
	return status == INSTALL_SUCCESS;
}

bool MultiROM::skipLine(const char *line)
{
	if(strstr(line, "mount"))
		return true;

	if(strstr(line, "format"))
		return true;

	if (strstr(line, "boot.img") || strstr(line, "/dev/block/mmcblk0p2") ||
		strstr(line, "/dev/block/platform/sdhci-tegra.3/by-name/LNX"))
		return true;

	return false;
}

bool MultiROM::prepareZIP(std::string& file)
{
	bool res = false;

	const ZipEntry *script_entry;
	int script_len;
	char* script_data;
	int itr = 0;
	char *token;

	char cmd[256];
	system("rm /tmp/mr_update.zip");
	sprintf(cmd, "cp \"%s\" /tmp/mr_update.zip", file.c_str());
	system(cmd);

	file = "/tmp/mr_update.zip";

	sprintf(cmd, "mkdir -p /tmp/%s", MR_UPDATE_SCRIPT_PATH);
	system(cmd);

	sprintf(cmd, "/tmp/%s", MR_UPDATE_SCRIPT_NAME);

	FILE *new_script = fopen(cmd, "w");
	if(!new_script)
		return false;

	ZipArchive zip;
	if (mzOpenZipArchive("/tmp/mr_update.zip", &zip) != 0)
		goto exit;

	script_entry = mzFindZipEntry(&zip, MR_UPDATE_SCRIPT_NAME);
	if(!script_entry)
		goto exit;

	if (read_data(&zip, script_entry, &script_data, &script_len) < 0)
		goto exit;

	mzCloseZipArchive(&zip);

	token = strtok(script_data, "\n");
	while(token)
	{
		if(!skipLine(token))
		{
			fputs(token, new_script);
			fputc('\n', new_script);
		}
		token = strtok(NULL, "\n");
	}

	free(script_data);
	fclose(new_script);

	sprintf(cmd, "cd /tmp && zip mr_update.zip %s", MR_UPDATE_SCRIPT_NAME);
	if(system(cmd) < 0)
		return false;
	return true;

exit:
	mzCloseZipArchive(&zip);
	fclose(new_script);
	return false;
}

#endif
