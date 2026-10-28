/* Bounds for the formatter's single-sector metadata file entries. */
#ifndef _UDF_META_BOUNDS_H_
#define _UDF_META_BOUNDS_H_

#include <stdint.h>

static inline int
udf_meta_efe_has_room(uint32_t sector_size, uint32_t header_size,
    uint32_t ea_length, uint32_t ad_length, uint32_t next_ad_size)
{
    if (sector_size < header_size ||
        ea_length > sector_size - header_size ||
        ad_length > sector_size - header_size - ea_length)
        return 0;
    return next_ad_size <= sector_size - header_size - ea_length - ad_length;
}

static inline int
udf_meta_efe_mapping_fits(uint64_t length, uint32_t sector_size,
    uint32_t blockingnr, uint32_t header_size, uint32_t ad_size,
    uint32_t max_extent_length)
{
    uint64_t extent_sectors, extent_bytes, slots;

    if (!sector_size || !blockingnr || !ad_size || sector_size < header_size)
        return 0;
    extent_sectors = max_extent_length / sector_size;
    extent_sectors = (extent_sectors / blockingnr) * blockingnr;
    extent_bytes = extent_sectors * sector_size;
    slots = (sector_size - header_size) / ad_size;
    return extent_bytes != 0 && length <= slots * extent_bytes;
}

#endif
