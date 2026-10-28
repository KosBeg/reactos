/* Validate a complete on-disk LVD map table before following any map. */
#ifndef _UDFS_LVD_MAP_VALIDATION_H_
#define _UDFS_LVD_MAP_VALIDATION_H_

static NTSTATUS
UDFValidateLogicalVolMaps(const struct LogicalVolDesc *lvd, uint32 BufferSize)
{
    const uint8 *maps;
    uint32 offset = 0, i;
    const struct UdfPartitionMap2 *sparable = NULL, *virtualMap = NULL;
    const struct GenericPartitionMap1 *physicalMap = NULL;
    uint32 metadataCount = 0;

    if (BufferSize < sizeof(*lvd) ||
        lvd->mapTableLength > BufferSize - sizeof(*lvd) ||
        !lvd->numPartitionMaps ||
        lvd->numPartitionMaps >
            lvd->mapTableLength / sizeof(struct GenericPartitionMap) ||
        lvd->descTag.descCRCLength > BufferSize - sizeof(tag) ||
        lvd->descTag.descCRCLength <
            sizeof(*lvd) - sizeof(tag) + lvd->mapTableLength)
        return STATUS_DISK_CORRUPT_ERROR;

    maps = (const uint8 *)(lvd + 1);
    for (i = 0; i < lvd->numPartitionMaps; ++i)
    {
        const struct GenericPartitionMap *gpm;
        uint32 remaining = lvd->mapTableLength - offset;

        if (remaining < sizeof(*gpm))
            return STATUS_DISK_CORRUPT_ERROR;
        gpm = (const struct GenericPartitionMap *)(maps + offset);
        if (gpm->partitionMapLength > remaining)
            return STATUS_DISK_CORRUPT_ERROR;
        if (gpm->partitionMapType == PARTITION_MAP_TYPE_1)
        {
            const struct GenericPartitionMap1 *physical = (const struct GenericPartitionMap1*)gpm;
            if (gpm->partitionMapLength != sizeof(struct GenericPartitionMap1))
                return STATUS_DISK_CORRUPT_ERROR;
            if (physicalMap && (physicalMap->volSeqNum != physical->volSeqNum ||
                                physicalMap->partitionNum != physical->partitionNum))
                return STATUS_NOT_SUPPORTED;
            physicalMap = physical;
        }
        else if (gpm->partitionMapType == PARTITION_MAP_TYPE_2)
        {
            const struct UdfPartitionMap2 *upm2;
            if (gpm->partitionMapLength != sizeof(struct UdfPartitionMap2))
                return STATUS_DISK_CORRUPT_ERROR;
            upm2 = (const struct UdfPartitionMap2 *)gpm;
            if (!strncmp((const char *)upm2->partIdent.ident,
                         UDF_ID_VIRTUAL, strlen(UDF_ID_VIRTUAL)))
            {
                const UDFIdentSuffix *suffix =
                    (const UDFIdentSuffix *)upm2->partIdent.identSuffix;
                if (virtualMap)
                    return STATUS_NOT_SUPPORTED;
                virtualMap = upm2;
                if (suffix->currentRev != 0x0150 &&
                    suffix->currentRev != 0x0200 &&
                    suffix->currentRev != 0x0201)
                    return STATUS_UNRECOGNIZED_VOLUME;
            }
            else if (!strncmp((const char *)upm2->partIdent.ident,
                              UDF_ID_METADATA, strlen(UDF_ID_METADATA)))
            {
                const METADATA_PARTITION_MAP *metadata =
                    (const METADATA_PARTITION_MAP *)gpm;
                if (++metadataCount > 1)
                    return STATUS_NOT_SUPPORTED;
                /* The native growth path cannot give zero units a safe
                 * meaning. The on-media allocation unit is a multiple of
                 * at least 32 blocks; alignment must be nonzero. */
                if (!metadata->allocationUnit ||
                    (metadata->allocationUnit % 32) ||
                    !metadata->alignmentUnit)
                    return STATUS_DISK_CORRUPT_ERROR;
            }
            else if (!strncmp((const char *)upm2->partIdent.ident,
                              UDF_ID_SPARABLE, strlen(UDF_ID_SPARABLE)))
            {
                if (sparable)
                    return STATUS_NOT_SUPPORTED;
                sparable = upm2;
            }
            else
                return STATUS_UNRECOGNIZED_VOLUME;
        }
        else
            return STATUS_UNRECOGNIZED_VOLUME;

        offset += gpm->partitionMapLength;
    }
    if (offset != lvd->mapTableLength)
        return STATUS_DISK_CORRUPT_ERROR;
    if (metadataCount) {
        /* The existing VCB owns one physical bitmap and one metadata view.
         * Multiple independent backing partitions require separate owners,
         * not shared partition-zero allocation accounting. */
        if (!physicalMap)
            return STATUS_NOT_SUPPORTED;
        for (i = 0, offset = 0; i < lvd->numPartitionMaps; ++i) {
            const struct GenericPartitionMap *map =
                (const struct GenericPartitionMap*)(maps + offset);
            if (map->partitionMapType == PARTITION_MAP_TYPE_2) {
                const struct UdfPartitionMap2 *typed = (const struct UdfPartitionMap2*)map;
                if (typed->volSeqNum != physicalMap->volSeqNum ||
                    typed->partitionNum != physicalMap->partitionNum)
                    return STATUS_NOT_SUPPORTED;
            }
            offset += map->partitionMapLength;
        }
    }
    if (sparable || virtualMap) {
        const struct UdfPartitionMap2 *special = sparable ? sparable : virtualMap;
        BOOLEAN hasPhysical = FALSE;
        /* VCB currently owns one sparing generation and one physical
         * allocation bitmap. Do not apply it to an unrelated partition
         * or compose it implicitly with VAT/metadata address spaces. */
        for (i = 0, offset = 0; i < lvd->numPartitionMaps; ++i) {
            const struct GenericPartitionMap *map =
                (const struct GenericPartitionMap *)(maps + offset);
            if ((const void*)map != (const void*)special) {
                const struct GenericPartitionMap1 *physical;
                if (map->partitionMapType != PARTITION_MAP_TYPE_1)
                    return STATUS_NOT_SUPPORTED;
                physical = (const struct GenericPartitionMap1*)map;
                if (physical->volSeqNum != special->volSeqNum ||
                    physical->partitionNum != special->partitionNum)
                    return STATUS_NOT_SUPPORTED;
                hasPhysical = TRUE;
            }
            offset += map->partitionMapLength;
        }
        if (virtualMap && !hasPhysical)
            return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

#endif
