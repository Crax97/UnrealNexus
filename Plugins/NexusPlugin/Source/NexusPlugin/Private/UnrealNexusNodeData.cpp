#include "UnrealNexusNodeData.h"
void SerializeNodeData(FArchive& Archive, nx::NodeData& NodeData, UINT32 NodeSize)
{
    Archive << NodeSize;

    // TODO: Replace with TArray<char>
    if (NodeData.memory == nullptr)
    {
        // Deserializing: Allocate memory
        NodeData.memory = new char[NodeSize];
    }
    
    for (UINT32 i = 0; i < NodeSize; i ++)
    {
        Archive << NodeData.memory[i];
    }
}

void UUnrealNexusNodeData::Serialize(FArchive& Archive)
{
    Super::Serialize(Archive);
    SerializeNodeData(Archive, NexusNodeData, NodeSize);
}
