// Fuzzes the CSV reader used for tile-join -c / tippecanoe-json-tool -c attribute joins.
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "csv.hpp"
#include "text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	// readcsv() calls exit() on a line that is not valid UTF-8; that is invalid input, so
	// reject it with the same check before the reader sees it.
	std::string all((const char *) data, size);
	size_t start = 0;
	while (start < all.size()) {
		size_t nl = all.find('\n', start);
		size_t end = nl == std::string::npos ? all.size() : nl + 1;
		if (check_utf8(all.substr(start, end - start)) != "") {
			return -1;
		}
		start = end;
	}

	int fd = memfd_create("csv", 0);
	if (fd < 0) {
		return 0;
	}
	size_t off = 0;
	while (off < size) {
		ssize_t w = write(fd, data + off, size - off);
		if (w <= 0) {
			close(fd);
			return 0;
		}
		off += w;
	}

	char path[64];
	snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

	std::vector<std::string> header;
	std::map<std::string, std::vector<std::string>> mapping;
	readcsv(path, header, mapping);
	close(fd);

	return 0;
}
