#include "UnrealNexusData.h"
#include "NexusUtils.h"

#include "corto/decoder.h"
// #include "space/intersection3.h"
// #include "space/line3.h"

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
    char* DataPtr = static_cast<char*>(Image->PlatformData->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    FMemory::Memcpy(DataPtr, Data.memory, Data.height * Data.width * 4);
    Image->PlatformData->Mips[0].BulkData.Unlock();
    Image->PlatformData->SetNumSlices(1);
    Image->NeverStream = true;
    Image->UpdateResource();
}

UUnrealNexusData::~UUnrealNexusData()
{
    Nodes = new Node[Header.n_nodes];
    Patches = new Patch[Header.n_patches];
    Textures = new Texture[Header.n_textures];
    NodeData = new nx::NodeData[Header.n_nodes];
    TextureData = new nx::TextureData[Header.n_textures];

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

    	Node &Node = Nodes[Candidate.Node];

    	if(Node.first_patch  == Sink) {
    		if(Candidate.Dist < Distance) {
    			Distance = Candidate.Dist;
    			Hit = true;
    		}
    		continue;
    	}

    	//not a leaf, insert children (if they intersect
    	for(uint32_t i = Node.first_patch; i < Node.last_patch(); i++) {
            const uint32_t Child = Patches[i].node;
    		if(Child == Sink) continue;
    		if(Visited[Child]) continue;

    		float d;
    		if(Closest(Nodes[Child].sphere, Ray, d)) {
    			Candidates.push_back(FDNode(Child, d));
    			push_heap(Candidates.begin(), Candidates.end());
    			Visited[Child] = true;
    		}
    	}
    }
    Distance = FMath::Sqrt(Distance);
    return Hit;
}

uint32_t UUnrealNexusData::Size(const uint32_t Node) const
{
    return Nodes[Node].getSize();
}

void UUnrealNexusData::Serialize(FArchive& Archive)
{
	Super::Serialize(Archive);
	SerializeHeader(Archive);
	SerializeNodes(Archive);
	SerializePatches(Archive);
	SerializeTextures(Archive);
}

void UUnrealNexusData::SerializeHeader(FArchive& Archive)
{
	Archive << Header.version;
	Archive << Header.nvert;
	Archive << Header.nface;
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

void UUnrealNexusData::SerializeAttribute(FArchive& Archive, const Attribute& Attribute)
{
	uint8 Type = Attribute.type;
	uint8 Number = Attribute.number;
	Archive.Serialize(reinterpret_cast<void*>(&Type), 1);
	Archive.Serialize(reinterpret_cast<void*>(&Number), 1);
}

void UUnrealNexusData::SerializeVertexAttributes(FArchive& Archive, const VertexElement& Vertex)
{
	for (int Index = 0; Index < 8; Index ++)
	{
		SerializeAttribute(Archive, Vertex.attributes[Index]);
	}
}

void UUnrealNexusData::SerializeFaceAttributes(FArchive& Archive, const FaceElement& Face)
{
	for (int Index = 0; Index < 8; Index ++)
	{
		SerializeAttribute(Archive, Face.attributes[Index]);
	}
}

void UUnrealNexusData::SerializeSphere(FArchive& Archive, const vcg::Sphere3f& Sphere)
{
	auto X = Sphere.Center().X();
	auto Y = Sphere.Center().Y();
	auto Z = Sphere.Center().Z();
	auto Radius = Sphere.Radius();
	Archive << X;
	Archive << Y;
	Archive << Z;
	Archive << Radius;
}

void UUnrealNexusData::SerializeNodes(FArchive& Archive) const
{
	for (uint32 i = 0; i < Header.n_nodes; i ++)
	{
		auto& Node = Nodes[i];
		Archive << Node.offset;
		Archive << Node.nvert;
		Archive << Node.nface;
		Archive << Node.error;
		Archive << Node.cone.n[0];
		Archive << Node.cone.n[1];
		Archive << Node.cone.n[2];
		Archive << Node.cone.n[3];
		Archive << Node.cone.n[0];
		SerializeSphere(Archive, Node.sphere);
		Archive << Node.tight_radius;
		Archive << Node.first_patch;
	}
}

void UUnrealNexusData::SerializePatches(FArchive& Archive) const
{
	for (uint32 i = 0; i < Header.n_patches; i ++)
	{
		auto& Patch = Patches[i];
		Archive << Patch.node;
		Archive << Patch.triangle_offset;
		Archive << Patch.texture;
	}
}

void UUnrealNexusData::SerializeTextures(FArchive& Archive) const
{
	for (uint32 i = 0; i < Header.n_textures; i ++)
	{
		auto& Texture = Textures[i];
		Archive << Texture.offset;
		for (int Mij = 0; Mij < 16; Mij ++)
		{
			Archive << Texture.matrix[Mij];
		}
	}
}

UUnrealNexusData::UUnrealNexusData() {}
