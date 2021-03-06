﻿#include "UnrealNexusNodeData.h"
#include "UnrealNexusData.h"

#include "NexusCommons.h"

void UUnrealNexusNodeData::DecodeData(Header& Header, const int VertsCount, const int FacesCount)
{
    if (DidDecodeData) return;
    LoadUtils::LoadNodeData(Header, VertsCount, FacesCount, NexusNodeData, NodeSize, UncompressedTexture);
    DidDecodeData = true;
}

void UUnrealNexusNodeData::SerializeNodeData(FArchive& Archive, nx::NodeData& NodeData)
{
    Archive << NodeSize;

    // TODO: Replace with TArray<char>
    if (NodeData.memory == nullptr)
    {
        // Deserializing: Allocate memory
        NodeData.memory = new char[NodeSize];
    }
    
    for (uint32 i = 0; i < NodeSize; i ++)
    {
        uint16 Index = NodeData.memory[i];
        Archive << Index;
        NodeData.memory[i] = Index;
    }
    
}

void UUnrealNexusNodeData::Serialize(FArchive& Archive)
{
    Super::Serialize(Archive);
    SerializeNodeData(Archive, NexusNodeData);
}
