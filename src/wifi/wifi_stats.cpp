#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../inc/radio_hal.h"
#include "wifi_hal.h"
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "debug.h"

static inline const char *wifi_get_dbgfs_basedir(enum wifi_driver_version drv_version)
{
    static const char *base_dir[] = { "ath9k", "ath10k", "ath11k", "",  ""};

    return base_dir[drv_version];
}

static int wifi_get_driver_version(struct wifi_softc *sc, int index)
{
	struct stat stats;
	char dir[64] = {0};
	int ret = 0;

	snprintf(dir, 64, "%s%s%s%s", "/sys/kernel/debug/ieee80211/", "phy", (char*)sc->nl_ctx.phyname[index], "/ath9k/");
	ret = stat(dir, &stats);

	if (!ret) {
		if (S_ISDIR(stats.st_mode)) {
			sc->nl_ctx.drv_version[index] = WIFI_DRIVER_ATH9K;
			return 0;
		}
	}

	memset(dir, 0,  64);
	snprintf(dir, 64, "%s%s%s%s", "/sys/kernel/debug/ieee80211/", "phy", (char*)sc->nl_ctx.phyname[index], "/ath10k/");
	ret = stat(dir, &stats);

	if (!ret) {
		if (S_ISDIR(stats.st_mode)) {
			sc->nl_ctx.drv_version[index] = WIFI_DRIVER_ATH10K;
			return 0;
		}
	}

	memset(dir, 0,  64);
	snprintf(dir, 64, "%s%s%s%s", "/sys/kernel/debug/ieee80211/", "phy", (char*)sc->nl_ctx.phyname[index], "/ath11k/");
	ret = stat(dir, &stats);

	if (!ret) {
		if (S_ISDIR(stats.st_mode)) {
			sc->nl_ctx.drv_version[index] = WIFI_DRIVER_ATH11K;
			return 0;
		}
	}

	memset(dir, 0,  64);
	snprintf(dir, 64, "%s", "sys/kernel/debug/brcmfmac/");
	ret = stat(dir, &stats);

	if (!ret)
		if (S_ISDIR(stats.st_mode)) {
			sc->nl_ctx.drv_version[index] = WIFI_DRIVER_BRCM_FMAC;
			return 0;
		}

	return -1;
}

int wifi_debugfs_init(struct wifi_softc *sc, int index)
{
	struct stat st;
	int ret;

	ret = wifi_get_driver_version(sc, index);
	if (ret<0)
		goto exit;
	/*To Do:  Check if debugfs is mounted */
	snprintf(sc->nl_ctx.debugfs_root[index], RADIO_DEBUGFS_DIRSIZE, "%s%s%s%s%s%s", "/sys/kernel/debug/ieee80211/", "phy", (char*)sc->nl_ctx.phyname[index], "/", wifi_get_dbgfs_basedir(sc->nl_ctx.drv_version[index]), "/");
	if (stat(sc->nl_ctx.debugfs_root[index], &st))
		goto exit;

	return 0;

exit:
	sc->nl_ctx.debugfs_root[index][0] = '\0';
	return -EACCES;
}

static FILE *wifi_debugfs_open(struct wifi_softc *sc, const char *filename, const char *mode, int index)
{
	char buf[1024];

	sprintf(buf, "%s/%s", sc->nl_ctx.debugfs_root[index], filename);

	return fopen(buf, mode);
}

int  wifi_debugfs_read(struct wifi_softc *sc, const char *filename, char *buf, int buf_size, int index)
{
	FILE *file;
	size_t n_read;

	file = wifi_debugfs_open(sc, filename, "r", index);
	if(!file)
		return -1;

	n_read = fread(buf, 1, buf_size - 1, file);

	buf[n_read] = '\0';

	fclose(file);

	return 0;
}

int  wifi_debugfs_write(struct wifi_softc *sc, const char *filename, const char *cmd, int index)
{

	int written = 0;
	FILE *file;

	file = wifi_debugfs_open(sc, filename, "w", index);
	if(!file)
		return -1;

	written = fwrite(cmd, 1, strlen(cmd), file);
	if ((size_t)written != strlen(cmd))
		hal_err(HAL_DBG_WIFI, "write is not matching with cmd param\n");

	fflush(file);
	fclose(file);

	return 0;
}

int wifi_debugfs_search(struct wifi_softc *sc, const char *filename, const char *substring, int index)
{
	FILE *file;
	size_t n = 0;
	char *line = NULL;
	int matched = 0;

	file = wifi_debugfs_open(sc, filename, "r", index);
	if(file)
		return -1;

	while (getline(&line, &n, file) >= 0) {
		matched = (strstr(line, substring) != NULL);
		if (matched)
			break;
	}

	free(line);
	fclose(file);

	return matched;
}

int wifi_get_fw_stats(struct wifi_softc *sc, char *buf, int buf_size, int index)
{
	return wifi_debugfs_read(sc, "fw_stats", buf, buf_size, index);
}

int wifi_capture_spectral_scan(struct wifi_softc *sc, int index)
{
	char filename[64];
	time_t rawtime;
	struct tm *timeinfo;
	FILE *fp;
	char *cmd;
        int ret = 0;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(filename, 64, "/var/log/spectral_data_%s", timeinfo);
	if (sc->nl_ctx.drv_version[index] == WIFI_DRIVER_ATH9K) {
                ret = wifi_debugfs_write(sc, "spectral_scan_ctl", "chanscan", index);
                if(ret)
                        goto error;
		wifi_hal_trigger_scan(sc, index);
		/* wait for scan to complete */
		sleep(2);
                ret = wifi_debugfs_write(sc, "spectral_scan_ctl", "disable", index);
                if(ret)
                        goto error;
		ret = asprintf(&cmd, "cat /sys/kernel/debug/ieee80211/phy%s/ath9k/%s%s", (char*)sc->nl_ctx.phyname[index], "spectral_scan0 > ", filename);
	} else if (sc->nl_ctx.drv_version[index] == WIFI_DRIVER_ATH10K) {
                ret = wifi_debugfs_write(sc, "spectral_scan_ctl", "background", index);
                if(ret)
                        goto error;
                ret = wifi_debugfs_write(sc, "spectral_scan_ctl", "trigger", index);
                if(ret)
                        goto error;
		wifi_hal_trigger_scan(sc, index);
		/* wait for scan to complete */
		sleep(2);
                ret = wifi_debugfs_write(sc, "spectral_scan_ctl", "disable", index);
                if(ret)
                        goto error;
		ret = asprintf(&cmd, "cat /sys/kernel/debug/ieee80211/phy%s/ath10k/%s%s", (char*)sc->nl_ctx.phyname[index], "spectral_scan0 > ", filename);
	} else {
		return -ENOTSUP;
	}

	fp = popen(cmd, "r");
	if (fp)
	        pclose(fp);

error:
        return ret;
}
