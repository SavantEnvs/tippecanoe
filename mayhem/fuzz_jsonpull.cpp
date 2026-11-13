// Fuzzes tippecanoe's streaming JSON parser (jsonpull) over arbitrary bytes, through both the
// tree reader (json_read_tree) and the node-at-a-time reader (json_read), then re-serializes.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <string>

#include "jsonpull/jsonpull.h"

struct memsource {
	const uint8_t *data;
	size_t size;
	size_t pos;
};

static ssize_t mem_read(json_pull *j, char *buf, size_t n) {
	memsource *m = (memsource *) j->source;
	size_t take = std::min(n, m->size - m->pos);
	if (take > 0) {
		memcpy(buf, m->data + m->pos, take);
		m->pos += take;
	}
	return take;
}

static volatile size_t sink;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	{
		memsource src = {data, size, 0};
		json_pull_ptr jp = json_begin(mem_read, &src);
		size_t trees = 0;
		while (trees++ < 64) {
			json_object_ptr tree = json_read_tree(jp);
			if (tree == nullptr) {
				break;
			}
			std::string s = json_stringify(tree.get());
			sink += s.size();
		}
	}

	{
		memsource src = {data, size, 0};
		json_pull_ptr jp = json_begin(mem_read, &src);
		json_object *o;
		while ((o = json_read(jp)) != nullptr) {
			if (o->type == JSON_HASH) {
				json_hash_get(o, "type");
			}
		}
	}

	return 0;
}
