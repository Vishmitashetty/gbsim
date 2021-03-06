/*
 * Greybus module manifest parsing
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Provided under the three clause BSD license found in the LICENSE file.
 */

#include <errno.h>

#include <stdbool.h>
#include <stdlib.h>
#include <linux/types.h>

#include <greybus.h>
#include <greybus_manifest.h>

#include "gbsim.h"

/*
 * Validate the given descriptor.  Its reported size must fit within
 * the number of bytes reamining, and it must have a recognized
 * type.  Check that the reported size is at least as big as what
 * we expect to see.  (It could be bigger, perhaps for a new version
 * of the format.)
 *
 * Returns the number of bytes consumed by the descriptor, or a
 * negative errno.
 */
static int identify_descriptor(struct greybus_descriptor *desc, size_t size)
{
	struct greybus_descriptor_header *desc_header = &desc->header;
	size_t expected_size;
	int desc_size;
	struct gbsim_cport *cport;

	if (size < sizeof(*desc_header)) {
		gbsim_error("manifest too small\n");
		return -EINVAL;		/* Must at least have header */
	}

	desc_size = (int)le16toh(desc_header->size);
	if ((size_t)desc_size > size) {
		gbsim_error("descriptor too big\n");
		return -EINVAL;
	}

	switch (desc_header->type) {
	case GREYBUS_TYPE_MODULE:
		if (desc_size < sizeof(struct greybus_descriptor_module)) {
			gbsim_error("module descriptor too small (%u)\n",
				desc_size);
			return -EINVAL;
		}
		break;
	case GREYBUS_TYPE_STRING:
		expected_size = sizeof(struct greybus_descriptor_header);
		expected_size += sizeof(struct greybus_descriptor_string);
		expected_size += (size_t)desc->string.length;
		if (desc_size < expected_size) {
			gbsim_error("string descriptor too small (%u)\n",
				desc_size);
			return -EINVAL;
		}
		break;
	case GREYBUS_TYPE_INTERFACE:
		break;
	case GREYBUS_TYPE_CPORT:
		if (desc_size < sizeof(struct greybus_descriptor_cport)) {
			gbsim_error("cport descriptor too small (%u)\n",
				desc_size);
			return -EINVAL;
		}
		cport = malloc(sizeof(struct gbsim_cport));
		cport->id = desc->cport.id;
		cport->protocol = desc->cport.protocol_id;
		TAILQ_INSERT_TAIL(&info.cports, cport, cnode);
		break;
	case GREYBUS_TYPE_CLASS:
		gbsim_debug("class descriptor found (ignoring)\n");
		break;
	case GREYBUS_TYPE_INVALID:
	default:
		gbsim_error("invalid descriptor type (%hhu)\n", desc_header->type);
		return -EINVAL;
	}

	return desc_size;
}

/*
 * Parse a buffer containing a module manifest.
 *
 * If we find anything wrong with the content/format of the buffer
 * we reject it.
 *
 * The first requirement is that the manifest's version is
 * one we can parse.
 *
 * We make an initial pass through the buffer and identify all of
 * the descriptors it contains, keeping track for each its type
 * and the location size of its data in the buffer.
 *
 * Returns true if parsing was successful, false otherwise.
 */
bool manifest_parse(void *data, size_t size)
{
	struct greybus_manifest *manifest;
	struct greybus_manifest_header *header;
	struct greybus_descriptor *desc;
	__u16 manifest_size;
	bool result = false;

	/* we have to have at _least_ the manifest header */
	if (size <= sizeof(manifest->header)) {
		gbsim_error("short manifest (%zu)\n", size);
		return false;
	}

	/* Make sure the size is right */
	manifest = data;
	header = &manifest->header;
	manifest_size = le16toh(header->size);
	if (manifest_size != size) {
		gbsim_error("manifest size mismatch %zu != %hu\n",
			size, manifest_size);
		return false;
	}

	/* Validate major/minor number */
	if (header->version_major > GREYBUS_VERSION_MAJOR) {
		gbsim_error("manifest version too new (%hhu.%hhu > %hhu.%hhu)\n",
			header->version_major, header->version_minor,
			GREYBUS_VERSION_MAJOR, GREYBUS_VERSION_MINOR);
		return false;
	}

	/* OK, find all the descriptors */
	desc = (struct greybus_descriptor *)(header + 1);
	size -= sizeof(*header);
	while (size) {
		int desc_size;

		desc_size = identify_descriptor(desc, size);
		if (desc_size <= 0) {
			if (!desc_size)
				gbsim_error("zero-sized manifest descriptor\n");
			result = false;
			goto out;
		}
		desc = (struct greybus_descriptor *)((char *)desc + desc_size);
		size -= desc_size;
	}

out:
	return result;
}
