#include <stdio.h>
#include <unistd.h>
#include <string>
#include <dirent.h>
#include <stdlib.h>
#include <linux/capability.h>
#include <linux/xattr.h>
#include <sys/xattr.h>
#include <sys/vfs.h>

#include "libcp_xattrs.h"

bool cp_xattrs_single_file(const std::string& from, const std::string& to)
{
    ssize_t res;
    char selabel[512];
    struct vfs_cap_data cap_data;

    res = lgetxattr(from.c_str(), "security.capability", &cap_data, sizeof(struct vfs_cap_data));
    if (res == sizeof(struct vfs_cap_data))
    {
        res = lsetxattr(to.c_str(), "security.capability", &cap_data, sizeof(struct vfs_cap_data), 0);
        if(res < 0 && errno != ENOENT)
        {
            fprintf(stderr, "Failed to lsetxattr capability on %s: %d (%s)\n", to.c_str(), errno, strerror(errno));
            return false;
        }
    }

    res = lgetxattr(from.c_str(), "security.selinux", selabel, sizeof(selabel));
    if(res > 0)
    {
        selabel[sizeof(selabel)-1] = 0;
        res = lsetxattr(to.c_str(), "security.selinux", selabel, strlen(selabel)+1, 0);
        if(res < 0 && errno != ENOENT)
        {
            fprintf(stderr, "Failed to lsetxattr selinux on %s: %d (%s)\n", to.c_str(), errno, strerror(errno));
            return false;
        }
    }
    else if(res < 0 && errno == ERANGE)
        fprintf(stderr, "lgetxattr selinux on %s failed: label is too long\n", from.c_str());

    return true;
}

bool cp_xattrs_recursive(const std::string& from, const std::string& to, unsigned char type)
{
    if(!cp_xattrs_single_file(from, to))
        return false;

    if(type != DT_DIR)
        return true;

    DIR *d;
    struct dirent *dt;

    d = opendir(from.c_str());
    if(!d)
    {
        fprintf(stderr, "Failed to open dir %s\n", from.c_str());
        return false;
    }

    while((dt = readdir(d)))
    {
        if (dt->d_type == DT_DIR && dt->d_name[0] == '.' &&
            (dt->d_name[1] == '.' || dt->d_name[1] == 0))
            continue;

        if(!cp_xattrs_recursive(from + "/" + dt->d_name, to + "/" + dt->d_name, dt->d_type))
        {
            closedir(d);
            return false;
        }
    }

    closedir(d);
    return true;
}
