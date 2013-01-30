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
#include "boot_img_hdr.h"
#include "data.hpp"

enum { INSTALL_SUCCESS, INSTALL_ERROR, INSTALL_CORRUPT };

enum
{
    ROM_DEFAULT           = 0,
    ROM_ANDROID_INTERNAL  = 1,
    ROM_UBUNTU_INTERNAL   = 2,
    ROM_ANDROID_USB_IMG   = 3,
    ROM_UBUNTU_USB_IMG    = 4,
    ROM_ANDROID_USB_DIR   = 5,
    ROM_UBUNTU_USB_DIR    = 6,

    ROM_UNSUPPORTED_INT   = 7,
    ROM_UNSUPPORTED_USB   = 8,
    ROM_UNKNOWN           = 9
};

#define M(x) (1 << x)
#define MASK_UBUNTU (M(ROM_UBUNTU_INTERNAL) | M(ROM_UBUNTU_USB_IMG)| M(ROM_UBUNTU_USB_DIR))
#define MASK_ANDROID (M(ROM_ANDROID_USB_DIR) | M(ROM_ANDROID_USB_IMG) | M(ROM_ANDROID_INTERNAL))
#define MASK_IMAGES (M(ROM_ANDROID_USB_IMG) | M(ROM_UBUNTU_USB_IMG))
#define MASK_INTERNAL (M(ROM_DEFAULT) | M(ROM_ANDROID_INTERNAL) | M(ROM_UBUNTU_INTERNAL) | M(ROM_UNSUPPORTED_INT))

#define INTERNAL_NAME "Internal"
#define REALDATA "/realdata"
#define MAX_ROM_NAME 26
#define INTERNAL_MEM_LOC_TXT "Internal memory"

// Not defined in android includes?
#define MS_RELATIME (1<<21)

class MultiROM
{
public:
	struct config {
		config() {
			current_rom = INTERNAL_NAME;
			auto_boot_seconds = 5;
			auto_boot_rom = INTERNAL_NAME;
			set_quiet_ubuntu = 1;
		}

		std::string current_rom;
		int auto_boot_seconds;
		std::string auto_boot_rom;
		int set_quiet_ubuntu;
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
	static bool injectBoot(std::string img_path);
	static int copyBoot(std::string& orig, std::string rom);
	static bool wipe(std::string name, std::string what);

	static config loadConfig();
	static void saveConfig(const config& cfg);

	static bool addROM(std::string zip, int os, std::string loc);

	static std::string listInstallLocations();
	static void setRomsPath(std::string loc);
	static bool patchInit(std::string name);
	static bool disableFlashKernelAct(std::string name, std::string loc);

private:
	static void findPath();
	static bool changeMounts(std::string base);
	static void restoreMounts();
	static bool prepareZIP(std::string& file);
	static bool skipLine(const char *line);
	static std::string getNewRomName(std::string zip);
	static bool createDirs(std::string name, int type);
	static bool androidExportBoot(std::string name, std::string zip, int type);
	static bool extractBootForROM(std::string base);
	static bool installFromBackup(std::string name, std::string path, int type);
	static bool extractBackupFile(std::string path, std::string part);
	static int getType(int os, std::string loc);

	static bool ubuntuExtractImage(std::string name, std::string img_path, std::string dest);
	static bool patchUbuntuInit(std::string rootDir);
	static bool ubuntuUpdateInitramfs(std::string rootDir);
	static void setUpChroot(bool start, std::string rootDir);
	static void ubuntuDisableFlashKernel(bool initChroot, std::string rootDir);
	static bool mountUbuntuImage(std::string name, std::string& dest);

	static bool createImage(std::string base, const char *img);
	
	static std::string m_path;
	static std::vector<file_backup> m_mount_bak;
	static std::string m_mount_rom_paths[2];
	static std::string m_curr_roms_path;
};

std::string MultiROM::m_path = "";
std::string MultiROM::m_mount_rom_paths[2] = { "", "" };
std::vector<MultiROM::file_backup> MultiROM::m_mount_bak;
std::string MultiROM::m_curr_roms_path = "";

bool MultiROM::folderExists()
{
	findPath();
	return !m_path.empty();
}

std::string MultiROM::getRomsPath()
{
	return m_curr_roms_path;
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
			m_curr_roms_path = m_path + "/roms/";
			return;
		}
	}
	m_path.clear();
}

void MultiROM::setRomsPath(std::string loc)
{
	umount("/mnt"); // umount last thing mounted there

	if(loc.compare(INTERNAL_MEM_LOC_TXT) == 0)
	{
		m_curr_roms_path = m_path + "/roms/";
		return;
	}

	size_t idx = loc.find(' ');
	if(idx == std::string::npos)
	{
		m_curr_roms_path.clear();
		return;
	}

	std::string dev = loc.substr(0, idx);
	mkdir("/mnt", 0777); // in case it does not exist

	char cmd[256];
	if(loc.find("(ntfs") == std::string::npos)
		sprintf(cmd, "mount %s /mnt", dev.c_str());
	else
		sprintf(cmd, "%s/ntfs-3g %s /mnt", m_path.c_str(), dev.c_str());
	system(cmd);

	m_curr_roms_path = "/mnt/multirom/";
	mkdir("/mnt/multirom/", 0777);
}

std::string MultiROM::listInstallLocations()
{
	std::string res = INTERNAL_MEM_LOC_TXT"\n";

	system("blkid > /tmp/blkid.txt");
	FILE *f = fopen("/tmp/blkid.txt", "r");
	if(!f)
		return res;

	char line[1024];
	std::string blk;
	size_t idx1, idx2;
	while((fgets(line, sizeof(line), f)))
	{
		if(!strstr(line, "/dev/block/sd"))
			continue;

		blk = line;
		idx1 = blk.find(':');
		if(idx1 == std::string::npos)
			continue;

		res += blk.substr(0, idx1);

		blk = line;
		idx1 = blk.find("TYPE=");
		if(idx1 == std::string::npos)
			continue;

		idx1 += strlen("TYPE=\"");
		idx2 = blk.find('"', idx1);
		if(idx2 == std::string::npos)
			continue;

		res += " (" + blk.substr(idx1, idx2-idx1) + ")\n";
	}

	fclose(f);
	return res;
}

bool MultiROM::move(std::string from, std::string to)
{
	std::string roms = getRomsPath();
	std::string cmd = "mv \"" + roms + "/" + from + "\" ";
	cmd += "\"" + roms + "/" + to + "\"";

	ui_print("Moving ROM \"%s\" to \"%s\"...\n", from.c_str(), to.c_str());

	return system(cmd.c_str()) == 0;
}

bool MultiROM::erase(std::string name)
{
	std::string path = getRomsPath() + "/" + name;

	ui_print("Erasing ROM \"%s\"...\n", name.c_str());
	std::string cmd = "rm -rf \"" + path + "\"";
	return system(cmd.c_str()) == 0;
}

bool MultiROM::wipe(std::string name, std::string what)
{
	ui_print("Changing mountpoints...\n");
	if(!changeMounts(name))
	{
		ui_print("Failed to change mountpoints!\n");
		return false;
	}

	char cmd[256];
	bool res = true;
	if(what == "dalvik")
	{
		static const char *dirs[] = {
			"data/dalvik-cache",
			"cache/dalvik-cache",
			"cache/dc",
		};

		for(uint8_t i = 0; res && i < sizeof(dirs)/sizeof(dirs[0]); ++i)
		{
			sprintf(cmd, "rm -rf \"/%s\"", dirs[i]);
			ui_print("Wiping dalvik: %s...\n", dirs[i]);
			res = (system(cmd) == 0);
		}
	}
	else
	{
		sprintf(cmd, "rm -rf \"/%s/\"*", what.c_str());
		ui_print("Wiping ROM's /%s...\n", what.c_str());
		res = (system(cmd) == 0);
	}

	sync();

	if(!res)
		ui_print("ERROR: Failed to erase %s!\n", what.c_str());

	ui_print("Restoring mountpoints...\n");
	restoreMounts();
	return res;
}

int MultiROM::getType(std::string name)
{
	std::string path = getRomsPath() + "/" + name + "/";
	struct stat info;

	if(getRomsPath().find("/mnt") != 0) // Internal memory
	{
		if (stat((path + "system").c_str(), &info) >= 0 &&
			stat((path + "data").c_str(), &info) >= 0 &&
			stat((path + "cache").c_str(), &info) >= 0)
			return ROM_ANDROID_INTERNAL;
		

		if(stat((path + "root").c_str(), &info) >= 0)
			return ROM_UBUNTU_INTERNAL;
	}
	else // USB roms
	{
		if (stat((path + "system").c_str(), &info) >= 0 &&
			stat((path + "data").c_str(), &info) >= 0 &&
			stat((path + "cache").c_str(), &info) >= 0)
			return ROM_ANDROID_USB_DIR;

		if (stat((path + "system.img").c_str(), &info) >= 0 &&
			stat((path + "data.img").c_str(), &info) >= 0 &&
			stat((path + "cache.img").c_str(), &info) >= 0)
			return ROM_ANDROID_USB_IMG;

		if(stat((path + "root").c_str(), &info) >= 0)
			return ROM_UBUNTU_USB_DIR;

		if(stat((path + "root.img").c_str(), &info) >= 0)
			return ROM_UBUNTU_USB_IMG;
	}
	return ROM_UNKNOWN;
}

static bool rom_sort(std::string a, std::string b)
{
	if(a == INTERNAL_NAME)
		return true;
	if(b == INTERNAL_NAME)
		return false;
	return a.compare(b) < 0;
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

			if(name == "current_rom")
				cfg.current_rom = val;
			else if(name == "auto_boot_seconds")
				cfg.auto_boot_seconds = atoi(val.c_str());
			else if(name == "auto_boot_rom")
				cfg.auto_boot_rom = val;
			else if(name == "set_quiet_ubuntu")
				cfg.set_quiet_ubuntu = atoi(val.c_str());
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

	fprintf(f, "current_rom=%s\n", cfg.current_rom.c_str());
	fprintf(f, "auto_boot_seconds=%d\n", cfg.auto_boot_seconds);
	fprintf(f, "auto_boot_rom=%s\n", cfg.auto_boot_rom.c_str());
	fprintf(f, "set_quiet_ubuntu=%d\n", cfg.set_quiet_ubuntu);

	fclose(f);
}

bool MultiROM::changeMounts(std::string name)
{
	int type = getType(name);
	std::string base = getRomsPath() + name;

	mkdir(REALDATA, 0777);
	if(mount("/dev/block/platform/sdhci-tegra.3/by-name/UDA",
	    REALDATA, "ext4", MS_RELATIME | MS_NOATIME,
		"user_xattr,acl,barrier=1,data=ordered") < 0)
	{
		ui_print("Failed to mount realdata: %d (%s)", errno, strerror(errno));
		return false;
	}

	if(M(type) & MASK_INTERNAL)
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

	// remove spaces from path
	size_t idx = base.find(' ');
	if(idx != std::string::npos)
		m_mount_rom_paths[0] = base;
	else
		m_mount_rom_paths[0].clear();

	while(idx != std::string::npos)
	{
		base.replace(idx, 1, "-");
		idx = base.find(' ', idx);
	}

	struct stat info;
	while(!m_mount_rom_paths[0].empty() && stat(base.c_str(), &info) >= 0)
		base += "a";

	if(!m_mount_rom_paths[0].empty())
	{
		m_mount_rom_paths[1] = base;
		std::string cmd = "mv \"" + m_mount_rom_paths[0] + "\" \"" + base + "\"";
		system(cmd.c_str());
	}

	fprintf(f_rec, "# mount point\tfstype\t\tdevice\n");
	if(!(M(type) & MASK_IMAGES))
	{
		fprintf(f_rec, "/system\t\text4\t\t%s/system\n", base.c_str());
		fprintf(f_rec, "/cache\t\text4\t\t%s/cache\n", base.c_str());
		fprintf(f_rec, "/data\t\text4\t\t%s/data\n", base.c_str());
	}
	else
	{
		fprintf(f_rec, "/system\t\text4\t\t%s/system.img\n", base.c_str());
		fprintf(f_rec, "/cache\t\text4\t\t%s/cache.img\n", base.c_str());
		fprintf(f_rec, "/data\t\text4\t\t%s/data.img\n", base.c_str());
	}
	fprintf(f_rec, "/misc\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/MSC\n");
	fprintf(f_rec, "/boot\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/LNX\n");
	fprintf(f_rec, "/recovery\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/SOS\n");
	fprintf(f_rec, "/staging\t\temmc\t\t/dev/block/platform/sdhci-tegra.3/by-name/USP\n");
	fprintf(f_rec, "/usb-otg\t\tvfat\t\t/dev/block/sda1\n");
	fclose(f_rec);

	if(!(M(type) & MASK_IMAGES))
	{
		fprintf(f_fstab, "%s/system /system ext4 rw,bind\n", base.c_str());
		fprintf(f_fstab, "%s/cache /cache ext4 rw,bind\n", base.c_str());
		fprintf(f_fstab, "%s/data /data ext4 rw,bind\n", base.c_str());
	}
	else
	{
		fprintf(f_fstab, "%s/system.img /system ext4 loop 0 0\n", base.c_str());
		fprintf(f_fstab, "%s/cache.img /cache ext4 loop 0 0\n", base.c_str());
		fprintf(f_fstab, "%s/data.img /data ext4 loop 0 0\n", base.c_str());
	}
	fprintf(f_fstab, "/usb-otg vfat rw\n");
	fclose(f_fstab);

	system("mount /system");
	system("mount /data");
	system("mount /cache");

	system("mv /sbin/umount /sbin/umount.bak");

	load_volume_table();
	return true;
}

void MultiROM::restoreMounts()
{
	system("mv /sbin/umount.bak /sbin/umount");
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

	if(!m_mount_rom_paths[0].empty())
	{
		std::string cmd = "mv \"" + m_mount_rom_paths[1] + "\" \"" + m_mount_rom_paths[0] + "\"";
		system(cmd.c_str());
		m_mount_rom_paths[0].clear();
	}

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

	ui_print("Preparing ZIP file...\n");
	if(!prepareZIP(file))
		return false;

	ui_print("Changing mountpoints\n");
	if(!changeMounts(rom))
	{
		ui_print("Failed to change mountpoints!\n");
		return false;
	}

	int wipe_cache = 0;
	int status = TWinstall_zip(file.c_str(), &wipe_cache);

	system("rm -r "MR_UPDATE_SCRIPT_PATH);
	system("rm /tmp/mr_update.zip");

	if(status != INSTALL_SUCCESS)
		ui_print("Failed to install ZIP!\n");
	else
		ui_print("ZIP successfully installed\n");

	restoreMounts();
	return (status == INSTALL_SUCCESS);
}

bool MultiROM::skipLine(const char *line)
{
	if(strstr(line, "mount") && !strstr(line, "bin/mount"))
		return true;

	if(strstr(line, "format"))
		return true;

	if(strstr(line, "/dev/block/platform/sdhci-tegra.3/"))
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

bool MultiROM::injectBoot(std::string img_path)
{
	char cmd[256];
	std::string path_trampoline = m_path + "/trampoline";
	struct stat info;

	if (stat(path_trampoline.c_str(), &info) < 0)
	{
		ui_print("%s not found!\n", path_trampoline.c_str());
		return false;
	}

	// EXTRACT BOOTIMG
	ui_print("Extracting boot image...\n");
	system("rm -r /tmp/boot; mkdir /tmp/boot");
	sprintf(cmd, "unpackbootimg -i \"%s\" -o /tmp/boot/", img_path.c_str());
	system(cmd);

	std::string p = img_path.substr(img_path.find_last_of("/")+1);
	sprintf(cmd, "/tmp/boot/%s-zImage", p.c_str());
	if(stat(cmd, &info) < 0)
	{
		ui_print("Failed to unpack boot img!\n");
		return false;
	}

	// DECOMPRESS RAMDISK
	ui_print("Decompressing ramdisk...\n");
	system("mkdir /tmp/boot/rd");
	sprintf(cmd, "cd /tmp/boot/rd && gzip -d -c ../%s-ramdisk.gz | cpio -i", p.c_str());
	system(cmd);
	if(stat("/tmp/boot/rd/init", &info) < 0)
	{
		ui_print("Failed to decompress ramdisk!\n");
		return false;
	}

	// COPY TRAMPOLINE
	ui_print("Copying trampoline...\n");
	if(stat("/tmp/boot/rd/main_init", &info) < 0)
		system("mv /tmp/boot/rd/init /tmp/boot/rd/main_init");

	sprintf(cmd, "cp \"%s\" /tmp/boot/rd/init", path_trampoline.c_str());
	system(cmd);
	system("chmod 750 /tmp/boot/rd/init");
	system("ln -sf ../main_init /tmp/boot/rd/sbin/ueventd");

	// COMPRESS RAMDISK
	ui_print("Compressing ramdisk...\n");
	sprintf(cmd, "cd /tmp/boot/rd && find . | cpio -o -H newc | gzip > ../%s-ramdisk.gz", p.c_str());
	system(cmd);

	// PACK BOOT IMG
	ui_print("Packing boot image\n");
	FILE *script = fopen("/tmp/boot/create.sh", "w");
	if(!script)
	{
		ui_print("Failed to open script file!\n");
		return false;
	}
	std::string base_cmd = "mkbootimg --kernel /tmp/boot/%s-zImage --ramdisk /tmp/boot/%s-ramdisk.gz "
		"--cmdline \"$(cat /tmp/boot/%s-cmdline)\" --base $(cat /tmp/boot/%s-base) --output /tmp/newboot.img\n";
	for(size_t idx = base_cmd.find("%s", 0); idx != std::string::npos; idx = base_cmd.find("%s", idx))
		base_cmd.replace(idx, 2, p);

	fputs(base_cmd.c_str(), script);
	fclose(script);

	system("chmod 777 /tmp/boot/create.sh && /tmp/boot/create.sh");
	if(stat("/tmp/newboot.img", &info) < 0)
	{
		ui_print("Failed to pack boot image!\n");
		return false;
	}
	system("rm -r /tmp/boot");
	if(img_path == "/dev/block/mmcblk0p2")
		system("dd bs=4096 if=/tmp/newboot.img of=/dev/block/mmcblk0p2");
	else
	{
		sprintf(cmd, "cp /tmp/newboot.img \"%s\"", img_path.c_str());;
		system(cmd);
	}
	return true;
}

int MultiROM::copyBoot(std::string& orig, std::string rom)
{
	std::string img_path = getRomsPath() + "/" + rom + "/boot.img";
	char cmd[256];
	sprintf(cmd, "cp \"%s\" \"%s\"", orig.c_str(), img_path.c_str());
	if(system(cmd) != 0)
		return 1;

	orig.swap(img_path);
	return 0;
}

std::string MultiROM::getNewRomName(std::string zip)
{
	std::string name = "ROM";
	size_t idx = zip.find_last_of("/");
	size_t idx_dot = zip.find_last_of(".");

	if(zip.substr(idx) == "/rootfs.img")
		name = "Ubuntu";
	else if(idx != std::string::npos && idx_dot != std::string::npos && idx_dot > idx)
		name = zip.substr(idx+1, idx_dot-idx-1);

	if(name.size() > MAX_ROM_NAME)
		name.resize(MAX_ROM_NAME);

	DIR *d = opendir(getRomsPath().c_str());
	if(!d)
		return "";

	std::vector<std::string> roms;
	struct dirent *dr;
	while((dr = readdir(d)))
	{
		if(dr->d_name[0] == '.')
			continue;

		if(dr->d_type != DT_DIR && dr->d_type != DT_LNK)
			continue;

		roms.push_back(dr->d_name);
	}

	closedir(d);

	std::string res = name;
	char num[8] = { 0 };
	int c = 1;
	for(size_t i = 0; i < roms.size();)
	{
		if(roms[i] == res)
		{
			res = name;
			sprintf(num, "%d", c++);
			if(res.size() + strlen(num) > MAX_ROM_NAME)
				res.replace(res.size()-strlen(num), strlen(num), num);
			else
				res += num;
			i = 0;
		}
		else
			++i;
	}

	return res;
}

bool MultiROM::createImage(std::string base, const char *img)
{
	ui_print("Creating %s.img...\n", img);
	
	char cmd[256];
	sprintf(cmd, "tw_multirom_%s_size", img);
	
	int size = DataManager::GetIntValue(cmd);
	if(size <= 0)
	{
		ui_printf("Failed to create %s image: invalid size (%d)\n", img, size);
		return false;
	}

	sprintf(cmd, "dd if=/dev/zero of=\"%s/%s.img\" bs=1M count=%d", base.c_str(), img, size);
	system(cmd);

	struct stat info;
	sprintf(cmd, "%s/%s.img", base.c_str(), img);
	if(stat(cmd, &info) < 0)
	{
		ui_print("Failed to create %s image, probably not enough space.\n", img);
		return false;
	}

	sprintf(cmd, "make_ext4fs -l %dM \"%s/%s.img\"", size, base.c_str(), img);
	system(cmd);
	return true;
}

bool MultiROM::createDirs(std::string name, int type)
{
	std::string base = getRomsPath() + "/" + name;
	if(mkdir(base.c_str(), 0777) < 0)
	{
		ui_print("Failed to create ROM folder!\n");
		return false;
	}

	ui_print("Creating folders and images for type %d\n", type);
	switch(type)
	{
		case ROM_ANDROID_USB_DIR:
		case ROM_ANDROID_INTERNAL:
			if (mkdir((base + "/boot").c_str(), 0777) < 0 ||
				mkdir((base + "/system").c_str(), 0755) < 0 ||
				mkdir((base + "/data").c_str(), 0771) < 0 ||
				mkdir((base + "/cache").c_str(), 0770) < 0)
			{
				ui_print("Failed to create android folders!\n");
				return false;
			}
			break;
		case ROM_ANDROID_USB_IMG:
			if (mkdir((base + "/boot").c_str(), 0777) < 0)
			{
				ui_print("Failed to create android folders!\n");
				return false;
			}

			static const char *imgs[] = { "system", "data", "cache" };
			for(size_t i = 0; i < sizeof(imgs)/sizeof(imgs[0]); ++i)
				if(!createImage(base, imgs[i]))
					return false;
			break;
		case ROM_UBUNTU_USB_DIR:
		case ROM_UBUNTU_INTERNAL:
			if (mkdir((base + "/root").c_str(), 0777) < 0)
			{
				ui_print("Failed to create ubuntu folders!\n");
				return false;
			}
			break;
		case ROM_UBUNTU_USB_IMG:
			if(!createImage(base, "root"))
				return false;
			break;
		default:
			ui_print("Unknown ROM type %d!\n", type);
			return false;
		
	}
	return true;
}

bool MultiROM::androidExportBoot(std::string name, std::string zip_path, int type)
{
	ui_print("Processing boot.img of ROM %s...\n", name.c_str());

	std::string base = getRomsPath() + "/" + name;
	char cmd[256];

	FILE *img = fopen((base + "/boot.img").c_str(), "w");
	if(!img)
	{
		ui_print("Failed to create boot.img!\n");
		return false;
	}

	ui_print("Extracting boot.img from ZIP file...\n");

	const ZipEntry *script_entry;
	int img_len;
	char* img_data = NULL;
	ZipArchive zip;

	if (mzOpenZipArchive(zip_path.c_str(), &zip) != 0)
	{
		ui_print("Failed to open zip file %s!\n", name.c_str());
		goto fail;
	}

	script_entry = mzFindZipEntry(&zip, "boot.img");
	if(!script_entry)
	{
		ui_printf("boot.img not found in the root of ZIP file!\n");
		goto fail;
	}

	if (read_data(&zip, script_entry, &img_data, &img_len) < 0)
	{
		ui_printf("Failed to read boot.img from ZIP!\n");
		goto fail;
	}

	fwrite(img_data, 1, img_len, img);
	fclose(img);
	mzCloseZipArchive(&zip);
	free(img_data);

	if(!extractBootForROM(base))
		return false;

	return true;

fail:
	mzCloseZipArchive(&zip);
	free(img_data);
	fclose(img);
	return false;
}

bool MultiROM::extractBootForROM(std::string base)
{
	char cmd[256];
	struct stat info;

	ui_printf("Extracting contents of boot.img...\n");
	sprintf(cmd, "unpackbootimg -i \"%s/boot.img\" -o \"%s/boot/\"", base.c_str(), base.c_str());
	system(cmd);

	sprintf(cmd, "%s/boot/boot.img-zImage", base.c_str());
	if(stat(cmd, &info) < 0)
	{
		ui_print("Failed to unpack boot.img!\n");
		return false;
	}

	static const char *keep[] = { "zImage", "ramdisk.gz", "cmdline", NULL };
	for(int i = 0; keep[i]; ++i)
	{
		sprintf(cmd, "mv \"%s/boot/boot.img-%s\" \"%s/boot/%s\"", base.c_str(), keep[i], base.c_str(), keep[i]);
		system(cmd);
	}

	sprintf(cmd, "rm \"%s/boot/boot.img-\"*", base.c_str());
	system(cmd);

	system("rm -r /tmp/boot");
	system("mkdir /tmp/boot");

	sprintf(cmd, "cd /tmp/boot && gzip -d -c \"%s/boot/ramdisk.gz\" | cpio -i", base.c_str());
	system(cmd);
	if(stat("/tmp/boot/init", &info) < 0)
	{
		ui_printf("Failed to extract ramdisk!\n");
		return false;
	}

	// copy rc files
	static const char *cp_f[] = { "*.rc", "default.prop", "init", "main_init", NULL };
	for(int i = 0; cp_f[i]; ++i)
	{
		sprintf(cmd, "cp -a /tmp/boot/%s \"%s/boot/\"", cp_f[i], base.c_str());
		system(cmd);
	}

	// check if main_init exists
	sprintf(cmd, "%s/boot/main_init", base.c_str());
	if(stat(cmd, &info) < 0)
	{
		sprintf(cmd, "mv \"%s/boot/init\" \"%s/boot/main_init\"", base.c_str(), base.c_str());
		system(cmd);
	}

	system("rm -r /tmp/boot");

	if (DataManager::GetIntValue("tw_multirom_share_kernel") == 0)
	{
		ui_printf("Injecting boot.img..\n");
		if(!injectBoot(base + "/boot.img") != 0)
			return false;
	}
	else
	{
		sprintf(cmd, "rm \"%s/boot.img\"", base.c_str());
		system(cmd);
	}
	return true;
}

bool MultiROM::ubuntuExtractImage(std::string name, std::string img_path, std::string dest)
{
	char cmd[256];
	struct stat info;

	if(img_path.find("img.gz") != std::string::npos)
	{
		ui_printf("Decompressing the image (may take a while)...\n");
		sprintf(cmd, "gzip -d \"%s\"", img_path.c_str());
		system(cmd);

		img_path.erase(img_path.size()-3);
		if(stat(img_path.c_str(), &info) < 0)
		{
			ui_print("Failed to decompress the image, more space needed?");
			return false;
		}
	}

	ui_printf("Converting the image (may take a while)...\n");
	sprintf(cmd, "simg2img \"%s\" /tmp/rootfs.img", img_path.c_str());
	system(cmd);

	system("mkdir /mnt_ub_img");
	system("umount /mnt_ub_img");
	system("mount /tmp/rootfs.img /mnt_ub_img");

	if(stat("/mnt_ub_img/rootfs.tar.gz", &info) < 0)
	{
		system("umount /mnt_ub_img");
		system("rm /tmp/rootfs.img");
		ui_printf("Invalid Ubuntu image (rootfs.tar.gz not found)!\n");
		return false;
	}

	ui_print("Extracting rootfs.tar.gz (will take a while)...\n");
	sprintf(cmd, "zcat /mnt_ub_img/rootfs.tar.gz | gnutar x --numeric-owner -C \"%s\"",  dest.c_str());
	system(cmd);

	sync();

	system("umount /mnt_ub_img");
	system("rm /tmp/rootfs.img");
	
	sprintf(cmd, "%s/boot/vmlinuz", dest.c_str());
	if(stat(cmd, &info) < 0)
	{
		ui_print("Failed to extract rootfs!\n");
		return false;
	}
	return true;
}

bool MultiROM::patchUbuntuInit(std::string rootDir)
{
	ui_print("Patching ubuntu init...\n");

	std::string initPath = rootDir + "/usr/share/initramfs-tools/";
	std::string locPath = rootDir + "/usr/share/initramfs-tools/scripts/";

	struct stat info;
	if(stat(initPath.c_str(), &info) < 0 || stat(locPath.c_str(), &info) < 0)
	{
		ui_printf("init paths do not exits\n");
		return false;
	}

	char cmd[256];
	sprintf(cmd, "cp -a \"%s/ubuntu-init/init\" \"%s\"", m_path.c_str(), initPath.c_str());
	system(cmd);
	sprintf(cmd, "cp -a \"%s/ubuntu-init/local\" \"%s\"", m_path.c_str(), locPath.c_str());
	system(cmd);

	sprintf(cmd, "echo \"none	 /proc 	proc 	nodev,noexec,nosuid 	0 	0\" > \"%s/etc/fstab\"", rootDir.c_str());
	system(cmd);
	return true;
}

void MultiROM::setUpChroot(bool start, std::string rootDir)
{
	char cmd[256];
	static const char *dirs[] = { "dev", "sys", "proc" };
	for(size_t i = 0; i < sizeof(dirs)/sizeof(dirs[0]); ++i)
	{
		if(start)
			sprintf(cmd, "mount -o bind /%s \"%s/%s\"", dirs[i], rootDir.c_str(), dirs[i]);
		else
			sprintf(cmd, "umount \"%s/%s\"", rootDir.c_str(), dirs[i]);
		system(cmd);
	}
}

bool MultiROM::ubuntuUpdateInitramfs(std::string rootDir)
{
	ui_print("Removing tarball installer...\n");

	setUpChroot(true, rootDir);

	char cmd[256];

	sprintf(cmd, "chroot \"%s\" apt-get -y --force-yes purge ac100-tarball-installer flash-kernel", rootDir.c_str());
	system(cmd);

	ubuntuDisableFlashKernel(false, rootDir);

	ui_print("Updating initramfs...\n");
	sprintf(cmd, "chroot \"%s\" update-initramfs -u", rootDir.c_str());
	system(cmd);

	// make proper link to initrd.img
	sprintf(cmd, "chroot \"%s\" bash -c 'cd /boot; ln -sf $(ls initrd.img-* | head -n1) initrd.img'", rootDir.c_str());
	system(cmd);

	setUpChroot(false, rootDir);
	return true;
}

void MultiROM::ubuntuDisableFlashKernel(bool initChroot, std::string rootDir)
{
	ui_print("Disabling flash-kernel");
	char cmd[256];
	if(initChroot)
	{
		setUpChroot(true, rootDir);
		sprintf(cmd, "chroot \"%s\" apt-get -y --force-yes purge flash-kernel", rootDir.c_str());
		system(cmd);
	}

	// We don't want flash-kernel to be active, ever.
	sprintf(cmd, "chroot \"%s\" bash -c \"echo flash-kernel hold | dpkg --set-selections\"", rootDir.c_str());
	system(cmd);

	sprintf(cmd, "if [ \"$(grep FLASH_KERNEL_SKIP '%s/etc/environment')\" == \"\" ]; then"
			"chroot \"%s\" bash -c \"echo FLASH_KERNEL_SKIP=1 >> /etc/environment\"; fi;",
			rootDir.c_str(), rootDir.c_str());
	system(cmd);

	if(initChroot)
		setUpChroot(false, rootDir);
}

bool MultiROM::disableFlashKernelAct(std::string name, std::string loc)
{
	int type = getType(2, loc);
	std::string dest = getRomsPath() + "/" + name + "/root";
	if(type == ROM_UBUNTU_USB_IMG && !mountUbuntuImage(name, dest))
		return false;

	ubuntuDisableFlashKernel(true, dest);

	sync();

	if(type == ROM_UBUNTU_USB_IMG)
		umount(dest.c_str());
	return true;
}

int MultiROM::getType(int os, std::string loc)
{
	switch(os)
	{
		case 1: // android
			if(loc == INTERNAL_MEM_LOC_TXT)
				return ROM_ANDROID_INTERNAL;
			else if(loc.find("(ext") != std::string::npos)
				return ROM_ANDROID_USB_DIR;
			else
				return ROM_ANDROID_USB_IMG;
			break;
		case 2: // ubuntu
			if(loc == INTERNAL_MEM_LOC_TXT)
				return ROM_UBUNTU_INTERNAL;
			else if(loc.find("(ext") != std::string::npos)
				return ROM_UBUNTU_USB_DIR;
			else
				return ROM_UBUNTU_USB_IMG;
			break;
	}
	return ROM_UNKNOWN;
}

bool MultiROM::mountUbuntuImage(std::string name, std::string& dest)
{
	mkdir("/mnt_ubuntu", 0777);

	char cmd[256];
	sprintf(cmd, "mount -o loop %s/%s/root.img /mnt_ubuntu", getRomsPath().c_str(), name.c_str());

	if(system(cmd) != 0)
	{
		ui_print("Failed to mount ubuntu image!\n");
		return false;
	}
	dest = "/mnt_ubuntu";
	return true;
}

bool MultiROM::addROM(std::string zip, int os, std::string loc)
{
	MultiROM::setRomsPath(loc);

	std::string name = getNewRomName(zip);
	if(name.empty())
	{
		ui_print("Failed to fixup ROMs name!\n");
		return false;
	}
	ui_print("Installing ROM %s...\n", name.c_str());
	
	int type = getType(os, loc);

	if(!createDirs(name, type))
		return false;

	bool res = false;
	switch(type)
	{
		case ROM_ANDROID_INTERNAL:
		case ROM_ANDROID_USB_DIR:
		case ROM_ANDROID_USB_IMG:
		{
			std::string src = DataManager::GetStrValue("tw_multirom_add_source");
			if(src == "zip")
			{
				if(!androidExportBoot(name, zip, type))
					break;
				if(!flashZip(name, zip))
					break;
			}
			else if(src == "backup")
			{
				if(!installFromBackup(name, zip, type))
					break;
			}
			else
			{
				ui_printf("Wrong source: %s\n", src.c_str());
				break;
			}
			res = true;
			break;
		}
		case ROM_UBUNTU_INTERNAL:
		case ROM_UBUNTU_USB_DIR:
		case ROM_UBUNTU_USB_IMG:
		{
			std::string dest = getRomsPath() + "/" + name + "/root";
			if(type == ROM_UBUNTU_USB_IMG && !mountUbuntuImage(name, dest))
				break;

			if (ubuntuExtractImage(name, zip, dest) &&
				patchUbuntuInit(dest) && ubuntuUpdateInitramfs(dest))
				res = true;

			char cmd[256];
			sprintf(cmd, "touch %s/var/lib/oem-config/run", dest.c_str());
			system(cmd);

			sprintf(cmd, "cp \"%s/infos/ubuntu.txt\" \"%s/%s/rom_info.txt\"",
					m_path.c_str(), getRomsPath().c_str(), name.c_str());
			system(cmd);

			if(type == ROM_UBUNTU_USB_IMG)
				umount(dest.c_str());
			break;
		}
	}

	if(!res)
	{
		ui_print("Erasing incomplete ROM...\n");
		std::string cmd = "rm -rf \"" + getRomsPath() + "/" + name + "\"";
		system(cmd.c_str());
	}

	sync();

	MultiROM::setRomsPath(INTERNAL_MEM_LOC_TXT);
	return res;
}

bool MultiROM::patchInit(std::string name)
{
	ui_print("Patching init for rom %s...\n", name.c_str());
	int type = getType(name);
	if(!(M(type) & MASK_UBUNTU))
	{
		ui_printf("This is not ubuntu ROM. (%d)\n", type);
		return false;
	}
	std::string dest;
	switch(type)
	{
		case ROM_UBUNTU_INTERNAL:
		case ROM_UBUNTU_USB_DIR:
			dest = getRomsPath() + name + "/root/";
			break;
		case ROM_UBUNTU_USB_IMG:
		{
			mkdir("/mnt_ubuntu", 0777);

			char cmd[256];
			sprintf(cmd, "mount -o loop %s/%s/root.img /mnt_ubuntu", getRomsPath().c_str(), name.c_str());

			if(system(cmd) != 0)
			{
				ui_print("Failed to mount ubuntu image!\n");
				return false;
			}
			dest = "/mnt_ubuntu/";
			break;
		}
	}

	bool res = false;
	if(patchUbuntuInit(dest) && ubuntuUpdateInitramfs(dest))
		res = true;
	
	sync();

	if(type == ROM_UBUNTU_USB_IMG)
		umount("/mnt_ubuntu");;
	return res;
}

bool MultiROM::installFromBackup(std::string name, std::string path, int type)
{
	struct stat info;
	char cmd[256];
	std::string base = getRomsPath() + "/" + name;
	int has_system = 0, has_data = 0;

	if(stat((path + "/boot.emmc.win").c_str(), &info) < 0)
	{
		ui_print("Backup must contain boot image!\n");
		return false;
	}

	DIR *d = opendir(path.c_str());
	if(!d)
	{
		ui_printf("Failed to list backup folder\n");
		return false;
	}

	struct dirent *dr;
	while((!has_system || !has_data) && (dr = readdir(d)))
	{
		if(strstr(dr->d_name, "system.ext4"))
			has_system = 1;
		else if(strstr(dr->d_name, "data.ext4"))
			has_data = 1;
	}
	closedir(d);
	
	if(!has_system)
	{
		ui_print("Backup must contain system image!\n");
		return false;
	}

	sprintf(cmd, "cp \"%s/boot.emmc.win\" \"%s/boot.img\"", path.c_str(), base.c_str());
	system(cmd);

	if(!extractBootForROM(base))
		return false;
	
	ui_print("Changing mountpoints\n");
	if(!changeMounts(name))
	{
		ui_print("Failed to change mountpoints!\n");
		return false;
	}

	// real /data is mounted to /realdata
	if(path.find("/data/media") == 0) 
		path.replace(0, 5, REALDATA);

	bool res = (extractBackupFile(path, "system") && (!has_data || extractBackupFile(path, "data")));
	restoreMounts();
	return res;
}

bool MultiROM::extractBackupFile(std::string path, std::string part)
{
	ui_printf("Extracting backup of %s partition...\n", part.c_str());

	struct stat info;
	std::string filename = part + ".ext4.win";
	std::string full_path =  path + "/" + filename;
	int index = 0;
	char split_index[5];
	char cmd[256];

	if (stat(full_path.c_str(), &info) < 0) // multiple archives
	{
		sprintf(split_index, "%03i", index);
		full_path = path + "/" + filename + split_index;
		while (stat(full_path.c_str(), &info) >= 0)
		{
			ui_print("Restoring archive %i...\n", ++index);

			sprintf(cmd, "cd /%s && gnutar -xf \"%s\"", part.c_str(), full_path.c_str());
			system(cmd);

			sprintf(split_index, "%03i", index);
			full_path = path + "/" + filename + split_index;
		}

		if (index == 0)
		{
			ui_printf("Failed to locate backup file %s\n", full_path.c_str());
			return false;
		}
	}
	else
	{
		sprintf(cmd, "cd /%s && gnutar -xf \"%s\"", part.c_str(), full_path.c_str());
		system(cmd);
	}
	return true;
}


#endif
