#pragma once
#include "GenericPlatform/GenericPlatform.h"
#include "Engine/StreamableManager.h"
#include "space/deprecated_point3.h"

namespace nx {
    class NodeData;
    struct Header;
}

DECLARE_LOG_CATEGORY_EXTERN(NexusInfo, Log, All)
DECLARE_LOG_CATEGORY_EXTERN(NexusErrors, Log, All)

namespace NexusCommons
{
    FVector VcgPoint3FToVector(const vcg::Point3f& Point3);
    FStreamableManager& GetStreamableManager();
}

namespace LoadUtils
{
    void LoadNodeData(nx::Header& Header, int VertCount, int FacesCount, nx::NodeData& TheNodeData, const UINT64 DataSizeOnDisk);
}