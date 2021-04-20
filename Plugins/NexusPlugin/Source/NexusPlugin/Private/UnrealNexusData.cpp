#include "UnrealNexusData.h"
#include "UnrealNexusNodeData.h"
#include "NexusCommons.h"

#include "corto/decoder.h"
#include "Engine/StreamableManager.h"
#include "HAL/FileManagerGeneric.h"
// #include "space/intersection3.h"
// #include "space/line3.h"

using namespace NexusCommons;

class FArchiveFileReaderGeneric;
DEFINE_LOG_CATEGORY(NexusInfo);
DEFINE_LOG_CATEGORY(NexusErrors);


using namespace nx;


class FDNode {
public:
    uint32_t Node;
    float Dist;
    explicit FDNode(const uint32_t N = 0, const float D = 0.0f): Node(N), Dist(D) {}
    bool operator<(const FDNode &N) const {
        return Dist < N.Dist;
    }
};

char* ToRGBA8888(char* RawData, const nx::TextureData& Data)
{
    // TODO: Transform from RGB to RGBA
    return RawData;
}

void LoadImageRawData(UTexture2D* Image, TextureData& Data)
{
    char* DataPtr = reinterpret_cast<char*>(Image->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memcpy(DataPtr, Data.memory, Data.height * Data.width * 4);
    Image->PlatformData->Mips[0].BulkData.Unlock();
    Image->PlatformData->SetNumSlices(1);
    Image->NeverStream = true;
    Image->UpdateResource();
}

UUnrealNexusData::~UUnrealNexusData()
{
}

vcg::Sphere3f& UUnrealNexusData::BoundingSphere()
{
    return Header.sphere;
}

bool Closest(vcg::Sphere3f &Sphere, vcg::Ray3f &Ray, float &Distance) {
#if 0
    vcg::Point3f Dir = Ray.Direction();
    const vcg::Line3f Line(Ray.Origin(), Dir.Normalize());

    vcg::Point3f p, q;
    const bool Success = vcg::IntersectionLineSphere(Sphere, Line, p, q);
    if(!Success) return false;
    p -= Ray.Origin();
    q -= Ray.Origin();
    float a = p*Ray.Direction();
    const float b = q*Ray.Direction();
    if(a > b) a = b;
    if(b < 0) return false; //behind observer
    if(a < 0) {   //we are inside
        Distance = 0;
        return true;
    }
    Distance = a;
#endif
    return true;
}

auto UUnrealNexusData::Intersects(vcg::Ray3f& Ray, float& Distance) -> bool
{
	const uint32_t Sink = Header.n_nodes -1;

    vcg::Point3f Dir = Ray.Direction();
    Ray.SetDirection(Dir.Normalize());

    if(!Closest(Header.sphere, Ray, Distance))
    	return false;

    Distance = 1e20f;

    //keep track of visited nodes (it is a DAG not a tree!
    std::vector<bool> Visited(Header.n_nodes, false);
    std::vector<FDNode> Candidates;      //heap of nodes
    Candidates.push_back(FDNode(0, Distance));

    bool Hit = false;

    while(Candidates.size()) {
    	pop_heap(Candidates.begin(), Candidates.end());
        const FDNode Candidate = Candidates.back();
    	Candidates.pop_back();

    	if(Candidate.Dist > Distance) break;
    	auto& UNode = Nodes[Candidate.Node];
    	Node& Node = UNode.NexusNode;

    	if(Node.first_patch  == Sink) {
    		if(Candidate.Dist < Distance) {
    			Distance = Candidate.Dist;
    			Hit = true;
    		}
    		continue;
    	}

    	//not a leaf, insert children (if they intersect
    	for(uint32_t i = Node.first_patch; i < Node.last_patch(); i++) {
            const uint32_t Child = UNode.NodePatches[i].node;
    		if(Child == Sink) continue;
    		if(Visited[Child]) continue;

    		nx::Node& ChildNode = Nodes[Child].NexusNode;
    		float d;
    		if(Closest(ChildNode.sphere, Ray, d)) {
    			Candidates.push_back(FDNode(Child, d));
    			push_heap(Candidates.begin(), Candidates.end());
    			Visited[Child] = true;
    		}
    	}
    }
    Distance = FMath::Sqrt(Distance);
    return Hit;
}

uint32_t UUnrealNexusData::Size(const uint32_t Node)
{
    return Nodes[Node].NexusNode.getSize();
}

void UUnrealNexusData::LoadNodeAsync(const uint32 NodeID, const FStreamableDelegate Callback)
{
	if (NodeHandles.Contains(NodeID))
	{
		auto& Handle = NodeHandles[NodeID];
		if (Handle->HasLoadCompleted())
		{
			if(!Callback.ExecuteIfBound()) {
				// Log this
			}
		}
	} else
	{
		const FSoftObjectPath NodePath = Nodes[NodeID].NodeDataPath;
		check(NodePath.IsValid());
		const auto Handle = GetStreamableManager().RequestAsyncLoad({NodePath}, Callback);
		NodeHandles.Add(NodeID, Handle);
	}
}

void UUnrealNexusData::UnloadNode(const int NodeID)
{	
	if(!NodeHandles.Contains(NodeID)) return;
	
	const FSoftObjectPath NodePath = Nodes[NodeID].NodeDataPath;
	GetStreamableManager().Unload({NodePath});
	NodeHandles.Remove(NodeID);
}

void UUnrealNexusData::LoadTextureForNode(const uint32 NodeID, FStreamableDelegate Callback)
{
	if (NodeHandles.Contains(NodeID))
	{
		auto& Handle = NodeHandles[NodeID];
		if (Handle->HasLoadCompleted())
		{
			if(!Callback.ExecuteIfBound()) {
				// Log this
			}
		}
	} else
	{
		const FSoftObjectPath NodePath = NodeTexturesPaths[NodeID];
		check(NodePath.IsValid());
		AsyncTask(ENamedThreads::GameThread, [NodePath, Callback, NodeID, this]() {
			const auto Handle = GetStreamableManager().RequestAsyncLoad({NodePath}, Callback);
			NodeTexturesHandles.Add(NodeID, Handle);
		});
	}
}

UTexture2D* UUnrealNexusData::GetTexture(const uint32 TextureID)
{
	const FSoftObjectPath TexturePath = NodeTexturesPaths[TextureID];
	if (!TexturePath.IsValid())
	{
		return Cast<UTexture2D>(GetStreamableManager().LoadSynchronous(TexturePath));
	}
	return Cast<UTexture2D>(TexturePath.ResolveObject());
}

void UUnrealNexusData::UnloadTexture(const int NodeID)
{
	const FSoftObjectPath NodePath = NodeTexturesPaths[NodeID];
	check(NodePath.IsValid());
	GetStreamableManager().Unload(NodePath);
}

UUnrealNexusNodeData* UUnrealNexusData::GetNode(const uint32 NodeId)
{
	const FSoftObjectPath NodePath = Nodes[NodeId].NodeDataPath;
	if (!NodePath.IsValid())
	{
		return Cast<UUnrealNexusNodeData>(GetStreamableManager().LoadSynchronous(NodePath));
	}
	return Cast<UUnrealNexusNodeData>(NodePath.ResolveObject());
}

void UUnrealNexusData::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);
	SerializeHeader(Archive);
	SerializeNodes(Archive);
	SerializeTextures(Archive);
}

void SerializeNodePatches(FArchive& Archive, TArray<nx::Patch>& NodePatches) 
{
	int PatchesCount = NodePatches.Num();
	Archive << PatchesCount;
	if (PatchesCount == NodePatches.Num())
	{
		for(auto& Patch : NodePatches)
		{
			Archive << Patch.node;
			Archive << Patch.triangle_offset;
			Archive << Patch.texture;
		}
	} else
	{
		for(int i = 0; i < PatchesCount; i ++)
		{
			nx::Patch Patch;
			Archive << Patch.node;
			Archive << Patch.triangle_offset;
			Archive << Patch.texture;
			NodePatches.Add(Patch);
		}		
	}
}

void SerializeNode(FArchive& Archive, nx::Node& Node)
{
	Archive << Node.offset;
	Archive << Node.nvert;
	Archive << Node.nface;
	Archive << Node.error;
	Archive << Node.cone.n[0];
	Archive << Node.cone.n[1];
	Archive << Node.cone.n[2];
	Archive << Node.cone.n[3];
	Archive << Node.sphere.Center().X();
	Archive << Node.sphere.Center().Y();
	Archive << Node.sphere.Center().Z();
	Archive << Node.sphere.Radius();
	Archive << Node.tight_radius;
	Archive << Node.first_patch;
}

void FUnrealNexusNode::Serialize(FArchive& Ar)
{
	SerializeNode(Ar, NexusNode);
	SerializeNodePatches(Ar, NodePatches);
	Ar << NodeDataPath;
}

void UUnrealNexusData::SerializeHeader(FArchive& Archive)
{
	Archive << Header.version;
	
	int64 NVert = *reinterpret_cast<int64*>(&Header.nvert);
	Archive << NVert;
	Header.nvert = *reinterpret_cast<uint64*>(&NVert);

	int64 NFace = *reinterpret_cast<int64*>(&Header.nface);
	Archive << NFace;
	Header.nvert = *reinterpret_cast<uint64*>(&NFace);
	
	SerializeSignature(Archive);
	Archive << Header.n_nodes;
	Archive << Header.n_patches;
	Archive << Header.n_textures;
	SerializeSphere(Archive, Header.sphere);
}

void UUnrealNexusData::SerializeSignature(FArchive& Archive)
{
	SerializeVertexAttributes(Archive, Header.signature.vertex);
	SerializeFaceAttributes(Archive, Header.signature.face);
	Archive << Header.signature.flags;
}

void UUnrealNexusData::SerializeAttribute(FArchive& Archive, Attribute& Attribute)
{
	Archive << Attribute.type;
	Archive <<  Attribute.number;
}

void UUnrealNexusData::SerializeVertexAttributes(FArchive& Archive, VertexElement& Vertex)
{
	for (int Index = 0; Index < 8; Index ++)
	{
		SerializeAttribute(Archive, Vertex.attributes[Index]);
	}
}

void UUnrealNexusData::SerializeFaceAttributes(FArchive& Archive, FaceElement& Face)
{
	for (int Index = 0; Index < 8; Index ++)
	{
		SerializeAttribute(Archive, Face.attributes[Index]);
	}
}

void UUnrealNexusData::SerializeSphere(FArchive& Archive, vcg::Sphere3f& Sphere)
{
	Archive << Sphere.Center().X();
	Archive << Sphere.Center().Y();
	Archive << Sphere.Center().Z();
	Archive << Sphere.Radius();
}

void UUnrealNexusData::SerializeNodes(FArchive& Archive)
{
	int NodesNum = Nodes.Num();
	Archive << NodesNum;
	if (NodesNum == Nodes.Num())
	{
		for (int i = 0; i < Nodes.Num(); i ++)
		{
			Nodes[i].Serialize(Archive);
		}
	} else
	{
		for (int i = 0; i < Nodes.Num(); i ++)
		{
			FUnrealNexusNode NewNode;
			NewNode.Serialize(Archive);
			Nodes.Add(NewNode);
		}
	}
}

void UUnrealNexusData::SerializeTextures(FArchive& Archive)
{
	int TextureNum = NodeTexturesPaths.Num();
	Archive << TextureNum;
	if (TextureNum == NodeTexturesPaths.Num())
	{
		for (auto& TexturePath : NodeTexturesPaths)
		{
			Archive << TexturePath;
		}
	} else
	{
		for (int i = 0; i < TextureNum; i ++)
		{
			FSoftObjectPath TexturePath;
			Archive << TexturePath;
			NodeTexturesPaths.Push(TexturePath);
		}
	}
	
}

UUnrealNexusData::UUnrealNexusData() {}
