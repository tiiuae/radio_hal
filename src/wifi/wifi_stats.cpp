#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../../inc/radio_hal.h"
#include "wifi_hal.h"
#include <sys/stat.h>

__attribute__((unused)) int wifi_debugfs_init(struct wifi_softc *sc, int index)
{
	struct stat st;

	/*To Do:  Check if debugfs is mounted */
	snprintf(sc->nl_ctx.debugfs_root, RADIO_DEBUGFS_DIRSIZE, "%s%s%s%s", "/sys/kernel/debug/ieee80211/", "phy", sc->nl_ctx.phyname[index], "/ath10k/");
	if (stat(sc->nl_ctx.debugfs_root, &st))
		goto exit;

	return 0;

exit:
	sc->nl_ctx.debugfs_root[0] = '\0';
	return -EACCES;
}

static FILE *wifi_debugfs_open(struct wifi_softc *sc, const char *filename, const char *mode)
{
	char buf[1024];

	sprintf(buf, "%s/%s", sc->nl_ctx.debugfs_root, filename);

	return fopen(buf, mode);
}

int  wifi_debugfs_read(struct wifi_softc *sc, const char *filename, char *buf, int buf_size)
{
	FILE *file;
	size_t n_read;

	file = wifi_debugfs_open(sc, filename, "r");
	if(!file)
		return -1;

	n_read = fread(buf, 1, buf_size - 1, file);

	buf[n_read] = '\0';

	fclose(file);

	return 0;
}

int  wifi_debugfs_write(struct wifi_softc *sc, const char *filename, const char *cmd)
{

	int written = 0;
	FILE *file;

	file = wifi_debugfs_open(sc, filename, "r");
	if(!file)
		return -1;

	written = fwrite(cmd, 1, strlen(cmd), file);
	if ((size_t)written != strlen(cmd))
		printf("write is not matching with cmd param\n");

	fflush(file);
	fclose(file);

	return 0;
}

int wifi_debugfs_search(struct wifi_softc *sc, const char *filename, const char *substring)
{
	FILE *file;
	size_t n = 0;
	char *line = NULL;
	int matched = 0;

	file = wifi_debugfs_open(sc, filename, "r");
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

int wifi_get_fw_stats(struct wifi_softc *sc, char *buf, int buf_size)
{
	return wifi_debugfs_read(sc, "fw_stats", buf, buf_size);
}

