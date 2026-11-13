// Fuzzes the Mapbox Vector Tile decoder (protobuf parse, optional gzip/zlib inflate, geometry
// command decoding) that tippecanoe-decode, tile-join and tippecanoe-overzoom use on input tiles.
#include <stddef.h>
#include <stdint.h>

#include <string.h>
#include <zlib.h>

#include <string>

#include "mvt.hpp"
#include "protozero/exception.hpp"

// Inflates a gzip/zlib tile the way mvt.cpp's decompress() does, but frees the zlib state on the
// failure path (decompress() leaks it, which is harmless in a CLI that exits but exhausts memory
// when the decoder runs in-process millions of times).
static bool inflate_tile(std::string const &in, std::string &out) {
	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit2(&zs, 32 + 15) != Z_OK) {
		return false;
	}
	zs.next_in = (Bytef *) in.data();
	zs.avail_in = in.size();

	char buf[16384];
	int ret;
	do {
		zs.next_out = (Bytef *) buf;
		zs.avail_out = sizeof(buf);
		ret = inflate(&zs, Z_NO_FLUSH);
		if (ret < 0 || ret == Z_NEED_DICT || out.size() > (64u << 20)) {
			inflateEnd(&zs);
			return false;
		}
		out.append(buf, sizeof(buf) - zs.avail_out);
	} while (ret != Z_STREAM_END);

	inflateEnd(&zs);
	return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	std::string message((const char *) data, size);

	// mvt_tile::decode() calls exit() on a compressed tile that fails to inflate; that is
	// invalid input, not a decoder defect, so reject it before the decoder sees it.
	if (is_compressed(message)) {
		std::string inflated;
		if (!inflate_tile(message, inflated) || is_compressed(inflated)) {
			return -1;
		}
		message = std::move(inflated);
	}

	// Nothing in tippecanoe catches protozero::exception, so a truncated protobuf makes every
	// tile consumer terminate(). That is an upstream defect the file-input decode_pbf and
	// overzoom targets report; here it is rejected so this harness can reach the memory-safety
	// paths behind the protobuf framing.
	mvt_tile tile;
	bool was_compressed;
	try {
		if (!tile.decode(message, was_compressed)) {
			return 0;
		}
	} catch (protozero::exception const &) {
		return -1;
	}

	size_t n = 0;
	for (auto const &layer : tile.layers) {
		n += layer.detail() > 0;
		for (auto const &f : layer.features) {
			n += f.geometry.size() + f.tags.size();
		}
	}
	return n == (size_t) -1;
}
